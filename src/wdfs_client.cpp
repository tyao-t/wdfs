//
// Starter code for CS 454/654
// You SHOULD change this file
//

#include "watdfs_client.h"
#include "watdfs_rpc.h"
#include "debug.h"
#include "rw_lock.h"
INIT_LOG

#include "rpc.h"
#include <map>
#include <iostream>
#include <cstring>
#include <string>
#include <cstdint>

using namespace std;

struct file_info {
    int flags;
    int fh;
    time_t tc;
};

typedef std::map<string, file_info> file_cache;

struct client_info {
    char *path_to_cache;
    time_t cache_interval;

    file_cache cache;
};

bool is_opened(void *userdata, char *local_path) {
    struct client_info *client = (client_info *)userdata;
    std::string path(local_path);
    if ((client->cache).find(path) != (client->cache).end()) {
        return true;
    } else {
        return false;
    }
}

int get_flags(void *userdata, char *local_path) {
    int flags = -1;
    struct client_info *client = (client_info *)userdata;
    std::string path(local_path);
    file_cache::iterator file;
    file = (client->cache).find(path);
    if (file != (client->cache).end()) {
        flags = (file->second).flags & O_ACCMODE;
    }

    return flags;
}

int get_fh(void *userdata, char *local_path) {
    int fh = -1;
    struct client_info *client = (client_info *)userdata;
    std::string path(local_path);
    file_cache::iterator file;
    file = (client->cache).find(path);
    if (file != (client->cache).end()) {
        fh = (file->second).fh;
    }

    return fh;
}

// SETUP AND TEARDOWN
void *watdfs_cli_init(struct fuse_conn_info *conn, const char *path_to_cache,
                      time_t cache_interval, int *ret_code) {
    // TODO: set up the RPC library by calling `rpcClientInit`.
    int initResult = rpcClientInit();
    if (initResult != 0) {
#ifdef PRINT_ERR
        std::cerr << "Failed to initialize RPC Client" << std::endl;
#endif
    }
    // TODO: check the return code of the `rpcClientInit` call
    // `rpcClientInit` may fail, for example, if an incorrect port was exported.

    // It may be useful to print to stderr or stdout during debugging.
    // Important: Make sure you turn off logging prior to submission!
    // One useful technique is to use pre-processor flags like:
    // # ifdef PRINT_ERR
    // std::cerr << "Failed to initialize RPC Client" << std::endl;
    // #endif
    // Tip: Try using a macro for the above to minimize the debugging code.

    // TODO Initialize any global state that you require for the assignment and return it.
    // The value that you return here will be passed as userdata in other functions.
    // In A1, you might not need it, so you can return `nullptr`.
    struct client_info *userdata = new struct client_info;

    // TODO: save `path_to_cache` and `cache_interval` (for A3).
    userdata->path_to_cache = new char[strlen(path_to_cache)+1];
    strcpy(userdata->path_to_cache, path_to_cache);
    userdata->cache_interval = cache_interval;
    // userdata->cache = new file_cache;

    // TODO: set `ret_code` to 0 if everything above succeeded else some appropriate
    // non-zero value.
    *ret_code = initResult;

    // Return pointer to global state data.
    return userdata;
}

void watdfs_cli_destroy(void *userdata) {
    struct client_info *client = (client_info *)userdata;
    // TODO: clean up your userdata state.
    delete client->path_to_cache;
    // delete userdata->cache;
    delete client;
    // TODO: tear down the RPC library by calling `rpcClientDestroy`.
    rpcClientDestroy();
}

char *get_full_path(void *userdata, const char *path) {
    struct client_info *client = (client_info *)userdata;
    const char *path_to_cache = client->path_to_cache;

    int short_path_len = strlen(path);
    int cache_dir_len = strlen(path_to_cache);
    int full_path_len = cache_dir_len + short_path_len + 1;

    char *full_path = new char[full_path_len];
    strcpy(full_path, path_to_cache);
    strcat(full_path, path);

    return full_path;
}

void update_tc(void *userdata, const char *path) {
    std::string local_path(get_full_path(userdata, path));
    struct client_info *client = (client_info *)userdata;
    file_cache::iterator file;
    file = (client->cache).find(local_path);
    if (file != (client->cache).end()) {
        (file->second).tc = time(0);
    }
}

bool check_freshness(void *userdata, const char *path) {
    int rpc_ret = 0;
    struct client_info *client = (client_info *)userdata;
    char *local_path = get_full_path(userdata, path);
    std::string open_file_local_path(local_path);
    time_t t = time(0);
    time_t tc = ((client->cache).find(open_file_local_path)->second).tc;

    if (t - tc < client->cache_interval) return true;

    struct stat server_statbuf;
    struct stat client_statbuf;

    rpc_ret = watdfs_rpc_getattr(userdata, path, &server_statbuf);
    rpc_ret = stat(local_path, &client_statbuf);
    if (rpc_ret < 0) {
      DLOG("Get file attributes failed");
    }

    if (client_statbuf.st_mtime == server_statbuf.st_mtime) {
        update_tc(userdata, path);
        return true;
    }

    delete local_path;

    return false;
}

// UPLOAD DOWNLOAD MODEL
int watdfs_cli_download(void *userdata, const char *path) {
    int rpc_ret, fxn_ret = 0;
    char *local_path = get_full_path(userdata, path);
    struct stat server_statbuf;
    int client_fh;

    // Step1: get attribute from the server
    rpc_ret = watdfs_rpc_getattr(userdata, path, &server_statbuf);
    
    // Step2: truncate the file at the client
    client_fh = open(local_path, O_RDWR);
    if (client_fh < 0) {
        rpc_ret = mknod(local_path, server_statbuf.st_mode, server_statbuf.st_dev);
        if (rpc_ret < 0) {
            DLOG("File creation failed with error '%d'", rpc_ret);
            fxn_ret = -errno;
        }
        client_fh = open(local_path, O_RDWR);
    }

    rpc_ret = truncate(local_path, server_statbuf.st_size);
    if (rpc_ret < 0) {
        fxn_ret = -errno;
    }

    // Step3: read file from the server
    struct fuse_file_info server_fi;
    char *buf = new char[server_statbuf.st_size];
    server_fi.flags = O_RDONLY;
    rpc_ret = watdfs_rpc_open(userdata, path, &server_fi);
    if (rpc_ret < 0) {
        // DLOG("watdfs_rpc_open called for '%s' failed 
        //            in read only mode with error '%d'", path, rpc_ret);
        fxn_ret = rpc_ret;
    }

    /*
        lock
    */
    rpc_ret = watdfs_rpc_lock(path, RW_READ_LOCK);
    if (rpc_ret < 0) {
        delete local_path;
        return rpc_ret;
    }

    rpc_ret = watdfs_rpc_read(userdata, path, buf, server_statbuf.st_size, 0, &server_fi);
    if (rpc_ret < 0) {
        DLOG("watdfs_rpc_read called for '%s' failed with error '%d'", path, rpc_ret);
        fxn_ret = rpc_ret;
    }

    /*
        unlock
    */
    rpc_ret = watdfs_rpc_unlock(path, RW_READ_LOCK);
    if (rpc_ret < 0) {
        fxn_ret = rpc_ret;
    }

    // Step4: write file to the client
    rpc_ret = pwrite(client_fh, buf, server_statbuf.st_size, 0);
    if (rpc_ret < 0) {
        DLOG("write operation from buf to the client file failed with error '%d'", rpc_ret);
        fxn_ret = -errno;
    }

    // Step5: update client file metadata
    struct timespec newts[2];
    newts[0] = server_statbuf.st_mtim;
    newts[1] = server_statbuf.st_mtim;
    rpc_ret = utimensat(0, local_path, newts, 0);
    if (rpc_ret < 0) {
        DLOG("utimensat operation called for '%s' failed with error '%d'", local_path, rpc_ret);
        fxn_ret = -errno;
    }

    rpc_ret = watdfs_rpc_release(userdata, path, &server_fi);
    if (rpc_ret < 0) {
        // DLOG("watdfs_rpc_release called for '%s' failed 
        //            in read only mode with error '%d'", path, rpc_ret);
        fxn_ret = rpc_ret;
    }
    rpc_ret = close(client_fh);
    if (rpc_ret < 0) {
        DLOG("close operation called for '%s' failed with error '%d'", local_path, rpc_ret);
        fxn_ret = -errno;
    }

    delete buf;
    delete local_path;

    return fxn_ret;
}

int watdfs_cli_upload(void *userdata, const char *path) {
    int rpc_ret, fxn_ret = 0;
    char *local_path = get_full_path(userdata, path);
    struct stat server_statbuf;
    struct stat client_statbuf;
    int client_fh;

    // Step1: get attributes from both sides
    rpc_ret = stat(local_path, &client_statbuf);
    if (rpc_ret < 0) {
        fxn_ret = -errno;
    }
    rpc_ret = watdfs_rpc_getattr(userdata, path, &server_statbuf);
    if (rpc_ret < 0) {
        // fxn_ret = rpc_ret;
        DLOG("File creation: target file is not on the server");
        rpc_ret = watdfs_rpc_mknod(userdata, path, 
                client_statbuf.st_mode, client_statbuf.st_dev);

        struct fuse_file_info server_fi;
        server_fi.flags = O_RDWR;
        rpc_ret = watdfs_rpc_open(userdata, path, &server_fi);
        if (rpc_ret < 0) {
            delete local_path;
            return rpc_ret;
        }
    }

    client_fh = open(local_path, O_RDONLY);
    if (client_fh < 0) {
        return -errno;
    }

    // Step2: read file from the client
    char *buf = new char[client_statbuf.st_size];
    rpc_ret = pread(client_fh, buf, client_statbuf.st_size, 0);
    if (rpc_ret < 0) {
        fxn_ret = -errno;
    }

    /*
        lock
    */

    rpc_ret = watdfs_rpc_lock(path, RW_WRITE_LOCK);
    if (rpc_ret < 0) {
        delete local_path;
        return rpc_ret;
    }

    // Step3: truncate the file at the server
    rpc_ret = watdfs_rpc_truncate(userdata, path, client_statbuf.st_size);
    if (rpc_ret < 0) {
        fxn_ret = rpc_ret;
    }

    // Step4: write file to the server
    rpc_ret = watdfs_rpc_write(userdata, path, buf, client_statbuf.st_size, 0, &server_fi);
    if (rpc_ret < 0) {
        fxn_ret = rpc_ret;
    }

    // Step5: update server file metadata
    struct timespec newts[2];
    newts[0] = client_statbuf.st_mtim;
    newts[1] = client_statbuf.st_mtim;
    rpc_ret = watdfs_rpc_utimens(userdata, path, newts);
    if (rpc_ret < 0) {
        DLOG("utimensat operation called for '%s' failed with error '%d'", path, rpc_ret);
        fxn_ret = rpc_ret;
    }

    /* 
        unlock
    */

    rpc_ret = watdfs_rpc_unlock(path, RW_WRITE_LOCK);
    if (rpc_ret < 0) {
        fxn_ret = rpc_ret;
    }

    /*
    rpc_ret = watdfs_rpc_release(userdata, path, &server_fi);
    if (rpc_ret < 0) {
        // DLOG("watdfs_rpc_release called for '%s' failed 
        //            in read only mode with error '%d'", path, rpc_ret);
        fxn_ret = rpc_ret;
    }
    */
    
    rpc_ret = close(client_fh);
    if (rpc_ret < 0) {
        DLOG("close operation called for '%s' failed with error '%d'", local_path, rpc_ret);
        fxn_ret = -errno;
    }


    delete local_path;
    delete buf;

    return fxn_ret;
}


// GET FILE ATTRIBUTES
int watdfs_cli_getattr(void *userdata, const char *path, struct stat *statbuf) {
    // SET UP THE RPC CALL
    DLOG("watdfs_cli_getattr called for '%s'", path);
    
    int rpc_ret, fxn_ret = 0;
    struct stat server_statbuf;
    int client_fh;
    char *local_path = get_full_path(userdata, path);

    if (is_opened(userdata, local_path)) {
        if (get_flags(userdata, local_path) == O_RDONLY) {
            if (check_freshness(userdata, path) == false) {
                rpc_ret = watdfs_cli_download(userdata, path);
                if (rpc_ret < 0) {
                    fxn_ret = rpc_ret;
                } else {
                    update_tc(userdata, path);
                }
            }
        }

        rpc_ret = stat(local_path, statbuf);
        if (rpc_ret < 0) {
            fxn_ret = -errno;
        }

    } else {
        rpc_ret = watdfs_rpc_getattr(userdata, path, &server_statbuf);
        if (rpc_ret < 0) {
            DLOG("watdfs_rpc_getattr failed for '%s', no such file exists on server", path);
            fxn_ret = rpc_ret;
        } else {
            rpc_ret = watdfs_cli_download(userdata, path);
            if (rpc_ret < 0) {
                DLOG("File caching failed for '%s'", path);
                fxn_ret = rpc_ret;
            } else {
                // check if file exists in watdfs_cli_download
                client_fh = open(local_path, O_RDONLY);
                rpc_ret = stat(local_path, statbuf);
                rpc_ret = close(client_fh);
                if (rpc_ret < 0) {
                    fxn_ret = rpc_ret;
                }
            }
        }
    }

    delete local_path;

    if (fxn_ret < 0) {
        // how to deal with statbuf
    }

    return fxn_ret;
}


// CREATE, OPEN AND CLOSE
int watdfs_cli_mknod(void *userdata, const char *path, mode_t mode, dev_t dev) {
    DLOG("watdfs_cli_mknod called for '%s'", path);
    
    int rpc_ret, fxn_ret = 0;
    char *local_path = get_full_path(userdata, path);

    if (is_opened(userdata, local_path)) {
        if (get_flags(userdata, local_path) == O_RDONLY) {
            DLOG("mknod operation is not allowed for file open in read only mode");
            fxn_ret = -EMFILE;
        } else {
            rpc_ret = mknod(local_path, mode, dev); 
            if (rpc_ret < 0) {
                DLOG("mknod operation failed: file already exists on client");
                fxn_ret = -errno;
            } else {
                if (check_freshness(userdata, path) == false) {
                    rpc_ret = watdfs_cli_upload(userdata, path);
                    if (rpc_ret < 0) {
                        fxn_ret = rpc_ret;
                    } else {
                        update_tc(userdata, path);
                    }
                }
            }
        }

    } else {
        rpc_ret = mknod(local_path, mode, dev); 
        if (rpc_ret < 0) {
            DLOG("mknod operation failed: file already exists on client");
            fxn_ret = -errno;
        } else {
            rpc_ret = watdfs_cli_upload(userdata, path);
            if (rpc_ret < 0) {
                fxn_ret = rpc_ret;
            }
        }
    }

    delete local_path;

    return fxn_ret;
}

int watdfs_cli_open(void *userdata, const char *path,
                    struct fuse_file_info *fi) {
    DLOG("watdfs_cli_open called for '%s'", path);
    
    int rpc_ret, fxn_ret = 0;
    struct stat server_statbuf;
    char *local_path = get_full_path(userdata, path);

    if (is_opened(userdata, local_path)) {
        fxn_ret = -EMFILE;
    } else {
        rpc_ret = watdfs_rpc_getattr(userdata, path, &server_statbuf);
        if (rpc_ret < 0) { // no such file exists on server, create the file
            if (fi->flags == O_CREAT) {
                rpc_ret = watdfs_rpc_open(userdata, path, fi);
                if (rpc_ret < 0) {
                    DLOG("Cannot open and create the file on server");
                    fxn_ret = rpc_ret;
                } else {
                    rpc_ret = watdfs_cli_download(userdata, path);
                    if (rpc_ret < 0) {
                        DLOG("Download operation fails");
                        fxn_ret = rpc_ret;
                    }
                }
            } else {
                fxn_ret = rpc_ret;
            }
        } else {
            rpc_ret = watdfs_cli_download(userdata, path);
            if (rpc_ret < 0) {
                fxn_ret = rpc_ret;
            }
            rpc_ret = watdfs_rpc_open(userdata, path, fi);
            if (rpc_ret < 0) {
                fxn_ret = rpc_ret;
            }
        }
    }

    if (fxn_ret < 0) {
        DLOG("watdfs_cli_open called for '%s' failed for error code '%d'", path, fxn_ret);
    } else {
        rpc_ret = open(local_path, fi->flags);
        if (rpc_ret < 0) {
            fxn_ret = -errno;
        } else {
            struct client_info *client = (client_info *)userdata;
            string open_file_local_path(local_path);
            (client->cache)[open_file_local_path].fh = rpc_ret;
            (client->cache)[open_file_local_path].flags = fi->flags;
            (client->cache)[open_file_local_path].tc = time(0);
        }
    }

    delete local_path;

    return fxn_ret;
}

int watdfs_cli_release(void *userdata, const char *path,
                       struct fuse_file_info *fi) {
    // Called during close, but possibly asynchronously.
    DLOG("watdfs_cli_release called for '%s'", path);
    
    int rpc_ret, fxn_ret = 0;
    int client_fh;
    char *local_path = get_full_path(userdata, path);

    rpc_ret = watdfs_rpc_release(userdata, path, fi);
    if (rpc_ret < 0) {
        fxn_ret = rpc_ret;
    }

    if (fi->flags != O_RDONLY) { // file was opened in write mode
        // the file should be flushed from the client to the server
        rpc_ret = watdfs_cli_upload(userdata, path);
        if (rpc_ret < 0) {
            fxn_ret = rpc_ret;
        }
    }

    struct client_info *client = (client_info *)userdata;
    std::string open_file_local_path(local_path);

    if (is_opened(userdata, local_path)) {
        client_fh = get_fh(userdata, local_path);
        rpc_ret = close(client_fh);
        if (rpc_ret < 0) {
            fxn_ret = -errno;
        } else {
            (client->cache).erase(open_file_local_path);
        }
    } else {
        fxn_ret = -ENOENT;
    }

    delete local_path;

    return fxn_ret;
}


// READ AND WRITE DATA
int watdfs_cli_read(void *userdata, const char *path, char *buf, size_t size,
                    off_t offset, struct fuse_file_info *fi) {
    // Read size amount of data at offset of file into buf.

    // Remember that size may be greater then the maximum array size of the RPC
    // library.
    DLOG("watdfs_cli_read called for '%s'", path);
    
    int rpc_ret, fxn_ret = 0;
    int client_fh;
    char *local_path = get_full_path(userdata, path);

    if (is_opened(userdata, local_path)) {
        // freshness check under read only mode
        if (get_flags(userdata, local_path) == O_RDONLY) {
            if (check_freshness(userdata, path) == false) {
                rpc_ret = watdfs_cli_download(userdata, path);
                if (rpc_ret < 0) {
                    fxn_ret = rpc_ret;
                }
                update_tc(userdata, path);
            }
        }
        client_fh = get_fh(userdata, local_path);
        rpc_ret = pread(client_fh, buf, size, offset);
        if (rpc_ret < 0) {
            fxn_ret = -errno;
        } else {
            DLOG("how many chars has been read: '%d'", rpc_ret);
            DLOG("chars read to buf is: '%s'", buf);
            DLOG("watdfs_cli_read success for '%s' ", path);

            fxn_ret = rpc_ret;
        }

    } else {
        DLOG("Read operation before open");
        fxn_ret = -EPERM; // error code for permission denied
    }

    delete local_path;

    return fxn_ret;
}

int watdfs_cli_write(void *userdata, const char *path, const char *buf,
                     size_t size, off_t offset, struct fuse_file_info *fi) {
    // Write size amount of data at offset of file from buf.

    // Remember that size may be greater then the maximum array size of the RPC
    // library.
    DLOG("watdfs_cli_write called for '%s'", path);
    int rpc_ret, fxn_ret = 0;
    int client_fh;
    char *local_path = get_full_path(userdata, path);

    if (is_opened(userdata, local_path)) {
        if (get_flags(userdata, local_path) != O_RDONLY) {
            client_fh = get_fh(userdata, local_path);
            rpc_ret = pwrite(client_fh, buf, size, offset);
            if (rpc_ret < 0) {
                DLOG("local write operation failed for '%s'", path);
                fxn_ret = -errno;
            } else {
                DLOG("how many chars has been read: '%d'", rpc_ret);
                DLOG("chars read to buf is: '%s'", buf);
                DLOG("watdfs_cli_write success for '%s' ", path);

	      	    fxn_ret = rpc_ret;		// write how many bits
                if (check_freshness(userdata, path) == false) {
                    rpc_ret = watdfs_cli_upload(userdata, path);
                    if (rpc_ret < 0) {
                        fxn_ret = rpc_ret;
                    }
                    // update local file metadata no matter file transfering was successful or not
                    update_tc(userdata, path);
                }
            }
        } else {
            fxn_ret = -EMFILE;
        }

    } else {
        fxn_ret = -EPERM;
    }
    
    delete local_path;

    return fxn_ret;
}

int watdfs_cli_truncate(void *userdata, const char *path, off_t newsize) {
    // Change the file size to newsize.
    DLOG("watdfs_cli_truncate called for '%s'", path);
    
    int rpc_ret, fxn_ret = 0;
    int client_fh;
    char *local_path = get_full_path(userdata, path);

    if (is_opened(userdata, local_path)) {
        if (get_flags(userdata, local_path) == O_RDONLY) {
            DLOG("truncate operation is not allowed for file open in read only mode");
            fxn_ret = -EMFILE;  // should it be -EACCES just like fsync???
        } else {
            rpc_ret = truncate(local_path, newsize); 
            if (rpc_ret < 0) {
                DLOG("local truncate operation failed");
                fxn_ret = -errno;
            } else {
                if (check_freshness(userdata, path) == false) {
                    rpc_ret = watdfs_cli_upload(userdata, path);
                    update_tc(userdata, path);
                    if (rpc_ret < 0) {
                        fxn_ret = rpc_ret;
                    }
                }
            }
        }
    } else {
        rpc_ret = watdfs_cli_download(userdata, path);
        client_fh = open(local_path, O_RDWR);
        rpc_ret = truncate(local_path, newsize); 
        if (rpc_ret < 0) {
            DLOG("local truncate operation failed");
            fxn_ret = -errno;
        }
        rpc_ret = close(client_fh);
    }

    delete local_path;

    return fxn_ret;
}

int watdfs_cli_fsync(void *userdata, const char *path,
                     struct fuse_file_info *fi) {
    // Force a flush of file data.
    DLOG("watdfs_cli_fsync called for '%s'", path);
    
    int rpc_ret, fxn_ret = 0;
    char *local_path = get_full_path(userdata, path);

    rpc_ret = watdfs_cli_upload(userdata, path);
    if (rpc_ret < 0) {
        fxn_ret = rpc_ret;
    } else {
        update_tc(userdata, path);
    }

    delete local_path;

    return fxn_ret;
}

// CHANGE METADATA
int watdfs_cli_utimens(void *userdata, const char *path,
                       const struct timespec ts[2]) {
    // Change file access and modification times.
    DLOG("watdfs_cli_utimens called for '%s'", path);

    int rpc_ret, fxn_ret = 0;
    int client_fh;
    struct stat server_statbuf;
    char *local_path = get_full_path(userdata, path);

    if (is_opened(userdata, local_path)) {
        if (get_flags(userdata, local_path) == O_RDONLY) {
            DLOG("utimens operation is not allowed for file open in read only mode");
            fxn_ret = -EMFILE;  
        } else {
            rpc_ret = utimensat(0, local_path, ts, 0); 
            if (rpc_ret < 0) {
                DLOG("local utimens operation failed");
                fxn_ret = -errno;
            } else {
                if (check_freshness(userdata, path) == false) {
                    rpc_ret = watdfs_cli_upload(userdata, path);
                    if (rpc_ret < 0) {
                        fxn_ret = rpc_ret;
                    } else {
                        update_tc(userdata, path);
                    }
                }
            }
        }
    } else {
        rpc_ret = watdfs_rpc_getattr(userdata, path, &server_statbuf);
        if (rpc_ret < 0) {
            fxn_ret = rpc_ret;
        } else {
            rpc_ret = watdfs_cli_download(userdata, path);
            client_fh = open(local_path, O_RDWR);
            if (client_fh < 0) {
                fxn_ret = client_fh;
            } else {
                rpc_ret = utimensat(0, local_path, ts, 0); 
                if (rpc_ret < 0) {
                    DLOG("local truncate operation failed");
                    fxn_ret = -errno;
                }
                rpc_ret = close(client_fh);
            }
        }
    }

    delete local_path;

    return fxn_ret;
}


/*
   ========================= local rpc calls ==========================
*/

int watdfs_rpc_lock(const char *path, rw_lock_mode_t mode) {
    DLOG("watdfs_rpc_lock called for '%s'", path);
    
    int ARG_COUNT = 3;
    void **args = new void*[ARG_COUNT];
    int arg_types[ARG_COUNT + 1];
    int pathlen = strlen(path) + 1;

    arg_types[0] =
        (1u << ARG_INPUT) | (1u << ARG_ARRAY) | (ARG_CHAR << 16u) | (uint) pathlen;
    args[0] = (void *)path;


    arg_types[1] = (1u << ARG_INPUT) | (ARG_INT << 16u); // mode
    args[1] = (void *) &mode;


    arg_types[2] = (1u << ARG_OUTPUT) | (ARG_INT << 16u); // return code
    int retcode = 0;
    args[2] = (int *) &retcode;
    
    arg_types[3] = 0;

    int rpc_ret = rpcCall((char *)"lock", arg_types, args);

    int fxn_ret = 0;
    if (rpc_ret < 0) {
        DLOG("lock rpc failed with error '%d'", rpc_ret);
        fxn_ret = -EINVAL;
    } else {
        fxn_ret = retcode;
    }

    if (fxn_ret < 0) {
        // Error msg
    }

    delete []args;

    return fxn_ret;
}

int watdfs_rpc_unlock(const char *path, rw_lock_mode_t mode) {
    DLOG("watdfs_rpc_unlock called for '%s'", path);

    int ARG_COUNT = 3;
    void **args = new void*[ARG_COUNT];
    int arg_types[ARG_COUNT + 1];
    int pathlen = strlen(path) + 1;

    arg_types[0] =
        (1u << ARG_INPUT) | (1u << ARG_ARRAY) | (ARG_CHAR << 16u) | (uint) pathlen;
    args[0] = (void *)path;


    arg_types[1] = (1u << ARG_INPUT) | (ARG_INT << 16u); // mode
    args[1] = (void *) &mode;


    arg_types[2] = (1u << ARG_OUTPUT) | (ARG_INT << 16u); // return code
    int retcode = 0;
    args[2] = (int *) &retcode;
    
    arg_types[3] = 0;

    int rpc_ret = rpcCall((char *)"unlock", arg_types, args);

    int fxn_ret = 0;
    if (rpc_ret < 0) {
        DLOG("unlock rpc failed with error '%d'", rpc_ret);
        fxn_ret = -EINVAL;
    } else {
        fxn_ret = retcode;
    }

    if (fxn_ret < 0) {
        // Error msg
    }

    delete []args;

    return fxn_ret;
}

// GET FILE ATTRIBUTES
int watdfs_rpc_getattr(void *userdata, const char *path, struct stat *statbuf) {
    // SET UP THE RPC CALL
    DLOG("watdfs_rpc_getattr called for '%s'", path);
    
    // getattr has 3 arguments.
    int ARG_COUNT = 3;

    // Allocate space for the output arguments.
    void **args = new void*[ARG_COUNT];

    // Allocate the space for arg types, and one extra space for the null
    // array element.
    int arg_types[ARG_COUNT + 1];

    // The path has string length (strlen) + 1 (for the null character).
    int pathlen = strlen(path) + 1;

    // Fill in the arguments
    // The first argument is the path, it is an input only argument, and a char
    // array. The length of the array is the length of the path.
    arg_types[0] =
        (1u << ARG_INPUT) | (1u << ARG_ARRAY) | (ARG_CHAR << 16u) | (uint) pathlen;
    // For arrays the argument is the array pointer, not a pointer to a pointer.
    args[0] = (void *)path;

    // The second argument is the stat structure. This argument is an output
    // only argument, and we treat it as a char array. The length of the array
    // is the size of the stat structure, which we can determine with sizeof.
    arg_types[1] = (1u << ARG_OUTPUT) | (1u << ARG_ARRAY) | (ARG_CHAR << 16u) |
                   (uint) sizeof(struct stat); // statbuf
    args[1] = (void *)statbuf;

    // The third argument is the return code, an output only argument, which is
    // an integer.
    // TODO: fill in this argument type.
    arg_types[2] = (1u << ARG_OUTPUT) | (ARG_INT << 16u);// return code
    int retcode = 0;
    args[2] = (int *) &retcode;
    // The return code is not an array, so we need to hand args[2] an int*.
    // The int* could be the address of an integer located on the stack, or use
    // a heap allocated integer, in which case it should be freed.
    // TODO: Fill in the argument

    // Finally, the last position of the arg types is 0. There is no
    // corresponding arg.
    arg_types[3] = 0;

    // MAKE THE RPC CALL
    int rpc_ret = rpcCall((char *)"getattr", arg_types, args);

    // HANDLE THE RETURN
    // The integer value watdfs_rpc_getattr will return.
    int fxn_ret = 0;
    if (rpc_ret < 0) {
        DLOG("getattr rpc failed with error '%d'", rpc_ret);
        // Something went wrong with the rpcCall, return a sensible return
        // value. In this case lets return, -EINVAL
        fxn_ret = -EINVAL;
    } else {
        // Our RPC call succeeded. However, it's possible that the return code
        // from the server is not 0, that is it may be -errno. Therefore, we
        // should set our function return value to the retcode from the server.

        // TODO: set the function return value to the return code from the server.
        fxn_ret = retcode;
    }

    if (fxn_ret < 0) {
        // If the return code of watdfs_rpc_getattr is negative (an error), then 
        // we need to make sure that the stat structure is filled with 0s. Otherwise,
        // FUSE will be confused by the contradicting return values.
        memset(statbuf, 0, sizeof(struct stat));
    }

    // Clean up the memory we have allocated.
    delete []args;

    // Finally return the value we got from the server.
    return fxn_ret;
}

// CREATE, OPEN AND CLOSE
int watdfs_rpc_mknod(void *userdata, const char *path, mode_t mode, dev_t dev) {
    DLOG("watdfs_rpc_mknod called for '%s'", path);
    
    int ARG_COUNT = 4;
    void **args = new void*[ARG_COUNT];
    int arg_types[ARG_COUNT + 1];
    int pathlen = strlen(path) + 1;

    arg_types[0] =
        (1u << ARG_INPUT) | (1u << ARG_ARRAY) | (ARG_CHAR << 16u) | (uint) pathlen;
    args[0] = (void *)path;


    arg_types[1] = (1u << ARG_INPUT) | (ARG_INT << 16u); // mode
    args[1] = (void *) &mode;

    arg_types[2] = (1u << ARG_INPUT) | (ARG_LONG << 16u); // dev
    args[2] = (void *) &dev;

    arg_types[3] = (1u << ARG_OUTPUT) | (ARG_INT << 16u); // return code
    int retcode = 0;
    args[3] = (int *) &retcode;
    
    arg_types[4] = 0;

    int rpc_ret = rpcCall((char *)"mknod", arg_types, args);

    int fxn_ret = 0;
    if (rpc_ret < 0) {
        DLOG("mknod rpc failed with error '%d'", rpc_ret);
        fxn_ret = -EINVAL;
    } else {
        fxn_ret = retcode;
    }

    if (fxn_ret < 0) {
        // Error msg
    }

    delete []args;

    return fxn_ret;
}
int watdfs_rpc_open(void *userdata, const char *path,
                    struct fuse_file_info *fi) {
    DLOG("watdfs_rpc_open called for '%s'", path);
    
    int ARG_COUNT = 3;
    void **args = new void*[ARG_COUNT];
    int arg_types[ARG_COUNT + 1];
    int pathlen = strlen(path) + 1;

    arg_types[0] =
        (1u << ARG_INPUT) | (1u << ARG_ARRAY) | (ARG_CHAR << 16u) | (uint) pathlen;
    args[0] = (void *)path;


    arg_types[1] = (1u << ARG_INPUT) | (1u << ARG_OUTPUT) | (1u << ARG_ARRAY) | (ARG_CHAR << 16u) |
                   (uint) sizeof(struct fuse_file_info); // fi
    args[1] = (void *) fi;

    arg_types[2] = (1u << ARG_OUTPUT) | (ARG_INT << 16u);   // return code
    int retcode = 0;
    args[2] = (int *) &retcode;
    
    arg_types[3] = 0;

    int rpc_ret = rpcCall((char *)"open", arg_types, args);

    int fxn_ret = 0;
    if (rpc_ret < 0) {
        DLOG("open rpc failed with error '%d'", rpc_ret);
        fxn_ret = -EINVAL;
    } else {
        fxn_ret = retcode;
    }

    if (fxn_ret < 0) {
        // Error msg
    }

    delete []args;

    return fxn_ret;
}

int watdfs_rpc_release(void *userdata, const char *path,
                       struct fuse_file_info *fi) {
    // Called during close, but possibly asynchronously.
    DLOG("watdfs_rpc_release called for '%s'", path);
    
    int ARG_COUNT = 3;
    void **args = new void*[ARG_COUNT];
    int arg_types[ARG_COUNT + 1];
    int pathlen = strlen(path) + 1;

    arg_types[0] =
        (1u << ARG_INPUT) | (1u << ARG_ARRAY) | (ARG_CHAR << 16u) | (uint) pathlen;
    args[0] = (void *)path;


    arg_types[1] = (1u << ARG_INPUT) | (1u << ARG_ARRAY) | (ARG_CHAR << 16u) |
                   (uint) sizeof(struct fuse_file_info); // fi
    args[1] = (void *) fi;

    arg_types[2] = (1u << ARG_OUTPUT) | (ARG_INT << 16u);   // return code
    int retcode = 0;
    args[2] = (int *) &retcode;
    
    arg_types[3] = 0;

    int rpc_ret = rpcCall((char *)"release", arg_types, args);

    int fxn_ret = 0;
    if (rpc_ret < 0) {
        DLOG("release rpc failed with error '%d'", rpc_ret);
        fxn_ret = -EINVAL;
    } else {
        fxn_ret = retcode;
    }

    if (fxn_ret < 0) {
        // Error msg
    }

    delete []args;

    return fxn_ret;
}

// READ AND WRITE DATA
int watdfs_rpc_read(void *userdata, const char *path, char *buf, size_t size,
                    off_t offset, struct fuse_file_info *fi) {
    // Read size amount of data at offset of file into buf.

    // Remember that size may be greater then the maximum array size of the RPC
    // library.
    DLOG("watdfs_rpc_read called for '%s'", path);
    
    int ARG_COUNT = 6;
    void **args = new void*[ARG_COUNT];
    int arg_types[ARG_COUNT + 1];
    int pathlen = strlen(path) + 1;

    arg_types[0] =
        (1u << ARG_INPUT) | (1u << ARG_ARRAY) | (ARG_CHAR << 16u) | (uint) pathlen;
    args[0] = (void *)path;

    arg_types[2] = (1u << ARG_INPUT) | (ARG_LONG << 16u); // size

    arg_types[3] = (1u << ARG_INPUT) | (ARG_LONG << 16u); // offset

    arg_types[4] = (1u << ARG_INPUT) | (1u << ARG_ARRAY) | (ARG_CHAR << 16u) |
                   (uint) sizeof(struct fuse_file_info); // fi
    args[4] = (void *) fi;

    arg_types[5] = (1u << ARG_OUTPUT) | (ARG_INT << 16u);   // return code
    int retcode = 0;
    args[5] = (int *) &retcode;
    
    arg_types[6] = 0;

    int rpc_ret = 0;
    int fxn_ret = 0;
    long newsize = MAX_ARRAY_LEN;
    
    while (size > MAX_ARRAY_LEN) {
      arg_types[1] = (1u << ARG_OUTPUT) | (1u << ARG_ARRAY) | (ARG_CHAR << 16u) | (uint) MAX_ARRAY_LEN;
      args[1] = (void *) buf;
      args[2] = (void *) &newsize;
      args[3] = (void *) &offset;

      rpc_ret = rpcCall((char *)"read", arg_types, args);

      if (rpc_ret  < 0) {
    fxn_ret = -EINVAL;
      } else {
    fxn_ret += retcode;
      }

      buf += MAX_ARRAY_LEN;
      offset += MAX_ARRAY_LEN;
      size -= MAX_ARRAY_LEN;
    }

    arg_types[1] = (1u << ARG_OUTPUT) | (1u << ARG_ARRAY) | (ARG_CHAR << 16u) | (uint) size;
    args[1] = (void *) buf;
    args[2] = (void *) &size;
    args[3] = (void *) &offset;

    rpc_ret = rpcCall((char *)"read", arg_types, args);

    if (rpc_ret < 0) {
        DLOG("read rpc failed with error '%d'", rpc_ret);
        fxn_ret = -EINVAL;
    } else {
        fxn_ret += retcode;
    }

    // fxn_ret += retcode;

    if (fxn_ret < 0) {
        // Error msg
    }

    delete []args;

    return fxn_ret;
}
int watdfs_rpc_write(void *userdata, const char *path, const char *buf,
                     size_t size, off_t offset, struct fuse_file_info *fi) {
    // Write size amount of data at offset of file from buf.

    // Remember that size may be greater then the maximum array size of the RPC
    // library.
    DLOG("watdfs_rpc_write called for '%s'", path);
    
    int ARG_COUNT = 6;
    void **args = new void*[ARG_COUNT];
    int arg_types[ARG_COUNT + 1];
    int pathlen = strlen(path) + 1;

    arg_types[0] =
        (1u << ARG_INPUT) | (1u << ARG_ARRAY) | (ARG_CHAR << 16u) | (uint) pathlen;
    args[0] = (void *)path;

    arg_types[2] = (1u << ARG_INPUT) | (ARG_LONG << 16u); // size

    arg_types[3] = (1u << ARG_INPUT) | (ARG_LONG << 16u); // offset

    arg_types[4] = (1u << ARG_INPUT) | (1u << ARG_ARRAY) | (ARG_CHAR << 16u) |
                   (uint) sizeof(struct fuse_file_info); // fi
    args[4] = (void *) fi;

    arg_types[5] = (1u << ARG_OUTPUT) | (ARG_INT << 16u);   // return code
    int retcode = 0;
    args[5] = (int *) &retcode;
    
    arg_types[6] = 0;

    long newsize = MAX_ARRAY_LEN;
    int rpc_ret = 0;
    int fxn_ret = 0;

    while (size > MAX_ARRAY_LEN) {
      arg_types[1] = (1u << ARG_INPUT) | (1u << ARG_ARRAY) | (ARG_CHAR << 16u) | (uint) MAX_ARRAY_LEN;
      args[1] = (void *) buf;
      args[2] = (void *) &newsize;
      args[3] = (void *) &offset;

      rpc_ret = rpcCall((char *)"write", arg_types, args);

      if (rpc_ret  < 0) {
        fxn_ret = -EINVAL;
      } else {
        fxn_ret += retcode;
      }

      buf += MAX_ARRAY_LEN;
      offset += MAX_ARRAY_LEN;
      size -= MAX_ARRAY_LEN;
    }

    arg_types[1] = (1u << ARG_INPUT) | (1u << ARG_ARRAY) | (ARG_CHAR << 16u) | (uint) size;
    args[1] = (void *) buf;
    args[2] = (void *) &size;
    args[3] = (void *) &offset;

    rpc_ret = rpcCall((char *)"write", arg_types, args);

    if (rpc_ret < 0) {
        DLOG("write rpc failed with error '%d'", rpc_ret);
        fxn_ret = -EINVAL;
    } else {
        fxn_ret += retcode;
    }

    // fxn_ret += retcode;

    if (fxn_ret < 0) {
        // Error msg
    }

    delete []args;

    return fxn_ret;
}
int watdfs_rpc_truncate(void *userdata, const char *path, off_t newsize) {
    // Change the file size to newsize.
    DLOG("watdfs_rpc_truncate called for '%s'", path);
    
    int ARG_COUNT = 3;
    void **args = new void*[ARG_COUNT];
    int arg_types[ARG_COUNT + 1];
    int pathlen = strlen(path) + 1;

    arg_types[0] =
        (1u << ARG_INPUT) | (1u << ARG_ARRAY) | (ARG_CHAR << 16u) | (uint) pathlen;
    args[0] = (void *)path;


    arg_types[1] = (1u << ARG_INPUT) | (ARG_LONG << 16u); // mode
    args[1] = (void *) &newsize;

    arg_types[2] = (1u << ARG_OUTPUT) | (ARG_INT << 16u); // return code
    int retcode = 0;
    args[2] = (int *) &retcode;
    
    arg_types[3] = 0;

    int rpc_ret = rpcCall((char *)"truncate", arg_types, args);

    int fxn_ret = 0;
    if (rpc_ret < 0) {
        DLOG("truncate rpc failed with error '%d'", rpc_ret);

        fxn_ret = -EINVAL;
    } else {
        fxn_ret = retcode;
    }

    if (fxn_ret < 0) {
        // Error msg
    }

    delete []args;

    return fxn_ret;
}

int watdfs_rpc_fsync(void *userdata, const char *path,
                     struct fuse_file_info *fi) {
    // Force a flush of file data.
    DLOG("watdfs_rpc_fsync called for '%s'", path);
    
    int ARG_COUNT = 3;
    void **args = new void*[ARG_COUNT];
    int arg_types[ARG_COUNT + 1];
    int pathlen = strlen(path) + 1;

    arg_types[0] =
        (1u << ARG_INPUT) | (1u << ARG_ARRAY) | (ARG_CHAR << 16u) | (uint) pathlen;
    args[0] = (void *)path;


    arg_types[1] = (1u << ARG_INPUT) | (1u << ARG_ARRAY) | (ARG_CHAR << 16u) |
                   (uint) sizeof(struct fuse_file_info); // fi
    args[1] = (void *) fi;

    arg_types[2] = (1u << ARG_OUTPUT) | (ARG_INT << 16u);   // return code
    int retcode = 0;
    args[2] = (int *) &retcode;
    
    arg_types[3] = 0;

    int rpc_ret = rpcCall((char *)"fsync", arg_types, args);

    int fxn_ret = 0;
    if (rpc_ret < 0) {
        DLOG("fsync rpc failed with error '%d'", rpc_ret);
        fxn_ret = -EINVAL;
    } else {
        fxn_ret = retcode;
    }

    if (fxn_ret < 0) {
        // Error msg
    }

    delete []args;

    return fxn_ret;
}

// CHANGE METADATA
int watdfs_rpc_utimens(void *userdata, const char *path,
                       const struct timespec ts[2]) {
    // Change file access and modification times.
    DLOG("watdfs_rpc_utimens called for '%s'", path);
    
    int ARG_COUNT = 3;
    void **args = new void*[ARG_COUNT];
    int arg_types[ARG_COUNT + 1];
    int pathlen = strlen(path) + 1;

    arg_types[0] =
        (1u << ARG_INPUT) | (1u << ARG_ARRAY) | (ARG_CHAR << 16u) | (uint) pathlen;
    args[0] = (void *)path;


    arg_types[1] = (1u << ARG_INPUT) | (1u << ARG_ARRAY) | (ARG_CHAR << 16u) |
                   (uint) 2*sizeof(struct timespec); // ts
    args[1] = (void *) ts;

    arg_types[2] = (1u << ARG_OUTPUT) | (ARG_INT << 16u);   // return code
    int retcode = 0;
    args[2] = (int *) &retcode;
    
    arg_types[3] = 0;

    int rpc_ret = rpcCall((char *)"utimens", arg_types, args);

    int fxn_ret = 0;
    if (rpc_ret < 0) {
        DLOG("utimens rpc failed with error '%d'", rpc_ret);
        fxn_ret = -EINVAL;
    } else {
        fxn_ret = retcode;
    }

    if (fxn_ret < 0) {
        // Error msg
    }

    delete []args;

    return fxn_ret;
}
