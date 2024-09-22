//
// Starter code for CS 454/654
// You SHOULD change this file
//

#include "rpc.h"
#include "debug.h"
INIT_LOG

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <cstring>
#include <cstdlib>
#include <fuse.h>
#include <string>
#include <map>
#include "rw_lock.h"

using namespace std;

// Global state server_persist_dir.
char *server_persist_dir = nullptr;

struct file_info {
    int open_mode;
    rw_lock_t *lock;
};

enum file_open_mode { RD_OPEN_MODE, RW_OPEN_MODE };

std::map<std::string, struct file_info *> global_file_state;

// Important: the server needs to handle multiple concurrent client requests.
// You have to be carefuly in handling global variables, esp. for updating them.
// Hint: use locks before you update any global variable.

// We need to operate on the path relative to the the server_persist_dir.
// This function returns a path that appends the given short path to the
// server_persist_dir. The character array is allocated on the heap, therefore
// it should be freed after use.
// Tip: update this function to return a unique_ptr for automatic memory management.
char *get_full_path(char *short_path) {
    int short_path_len = strlen(short_path);
    int dir_len = strlen(server_persist_dir);
    int full_len = dir_len + short_path_len + 1;

    char *full_path = (char *)malloc(full_len);

    // First fill in the directory.
    strcpy(full_path, server_persist_dir);
    // Then append the path.
    strcat(full_path, short_path);
    DLOG("Full path: %s\n", full_path);

    return full_path;
}

// The server implementation of getattr.
int watdfs_getattr(int *argTypes, void **args) {
    // Get the arguments.
    // The first argument is the path relative to the mountpoint.
    char *short_path = (char *)args[0];
    // The second argument is the stat structure, which should be filled in
    // by this function.
    struct stat *statbuf = (struct stat *)args[1];
    // The third argument is the return code, which should be set be 0 or -errno.
    int *ret = (int *)args[2];

    // Get the local file name, so we call our helper function which appends
    // the server_persist_dir to the given path.
    char *full_path = get_full_path(short_path);

    // Initially we set set the return code to be 0.
    *ret = 0;

    // TODO: Make the stat system call, which is the corresponding system call needed
    // to support getattr. You should use the statbuf as an argument to the stat system call.
    // (void)statbuf;

    // Let sys_ret be the return code from the stat system call.
    int sys_ret = stat(full_path, statbuf);

    if (sys_ret < 0) {
        // If there is an error on the system call, then the return code should
        // be -errno.
        *ret = -errno;
    }

    // Clean up the full path, it was allocated on the heap.
    free(full_path);

    //DLOG("Returning code: %d", *ret);
    // The RPC call succeeded, so return 0.
    return 0;
}

int watdfs_lock(int *argTypes, void **args) {
    char *short_path = (char *)args[0];
    rw_lock_mode_t mode = *((rw_lock_mode_t *)args[1]);
    int *ret = (int *)args[2];

    *ret = 0;
    std::string s_path(short_path);
    std::map<std::string, struct file_info *>::iterator file = global_file_state.find(s_path);
    int sys_ret = rw_lock_lock((file->second)->lock, mode);

    if (sys_ret < 0) {
        *ret = -errno;
    }

    return 0;
}

int watdfs_unlock(int *argTypes, void **args) {
    char *short_path = (char *)args[0];
    rw_lock_mode_t mode = *((rw_lock_mode_t *)args[1]);
    int *ret = (int *)args[2];

    *ret = 0;
    std::string s_path(short_path);
    std::map<std::string, struct file_info *>::iterator file = global_file_state.find(s_path);
    int sys_ret = rw_lock_unlock((file->second)->lock, mode);

    if (sys_ret < 0) {
        *ret = -errno;
    }

    return 0;
}

int watdfs_mknod(int *argTypes, void **args) {
    char *short_path = (char *)args[0];
    mode_t mode = *((mode_t *)args[1]);
    dev_t dev = *((dev_t *)args[2]);
    int *ret = (int *)args[3];
    char *full_path = get_full_path(short_path);

    *ret = 0;
    int sys_ret = mknod(full_path, mode, dev);

    if (sys_ret < 0) {
        *ret = -errno;
    }

    free(full_path);

    return 0;
}

int watdfs_open(int *argTypes, void **args) {
    char *short_path = (char *)args[0];
    struct fuse_file_info *fi = (struct fuse_file_info *)args[1];
    int *ret = (int *)args[2];
    char *full_path = get_full_path(short_path);

    *ret = 0;
    std::string s_path(short_path);

    if (global_file_state.find(s_path) != global_file_state.end()) {
        if ((fi->flags & O_ACCMODE) == O_RDWR) {
            if (global_file_state[s_path]->open_mode != RD_OPEN_MODE) {
                *ret = -EACCES;
            } else {
                global_file_state[s_path]->open_mode = RW_OPEN_MODE;
            }
        }
    } else {
        global_file_state[s_path] = new struct file_info;

        if ((fi->flags & O_ACCMODE) != O_RDONLY) {
            global_file_state[s_path]->open_mode = RW_OPEN_MODE;
        } else {
            global_file_state[s_path]->open_mode = RD_OPEN_MODE;
        }

        global_file_state[s_path]->lock = new rw_lock_t;
        rw_lock_init(global_file_state[s_path]->lock);
    }


    int sys_ret = open(full_path, fi->flags);

    if (sys_ret < 0) {
        *ret = -errno;
    }

    fi->fh = sys_ret;

    free(full_path);

    return 0;
}

int watdfs_release(int *argTypes, void **args) {
    char *short_path = (char *)args[0];
    struct fuse_file_info *fi = (struct fuse_file_info *)args[1];
    int *ret = (int *)args[2];
    std::string s_path(short_path);

    *ret = 0;

    std::map<std::string, struct file_info *>::iterator file;
    file = global_file_state.find(s_path);
    if (file != global_file_state.end()) {
        global_file_state.erase(file);
    }

    int sys_ret = close(fi->fh);

    if (sys_ret < 0) {
        *ret = -errno;
    }

    return 0;
}

int watdfs_read(int *argTypes, void **args) {
    char *short_path = (char *)args[0];
    char *buf = (char *)args[1];
    size_t size = *((size_t *) args[2]);
    off_t offset = *((off_t *) args[3]);
    struct fuse_file_info *fi = (struct fuse_file_info *)args[4];
    int *ret = (int *)args[5];
    char *full_path = get_full_path(short_path);

    *ret = 0;
    int sys_ret = pread(fi->fh, buf, size, offset);

    if (sys_ret < 0) {
        *ret = -errno;
    } else {
      	*ret = sys_ret;
    }

    free(full_path);

    return 0;
}

int watdfs_write(int *argTypes, void **args) {
    char *short_path = (char *)args[0];
    const char *buf = (char *)args[1];
    size_t size = *((size_t *) args[2]);
    off_t offset = *((off_t *) args[3]);
    struct fuse_file_info *fi = (struct fuse_file_info *)args[4];
    int *ret = (int *)args[5];
    char *full_path = get_full_path(short_path);

    *ret = 0;
    int sys_ret = pwrite(fi->fh, buf, size, offset);

    if (sys_ret < 0) {
        *ret = -errno;
    } else {
      	*ret = sys_ret;
    }

    free(full_path);

    return 0;
}

int watdfs_truncate(int *argTypes, void **args) {
    char *short_path = (char *)args[0];
    off_t newsize = *((off_t *)args[1]);
    int *ret = (int *)args[2];
    char *full_path = get_full_path(short_path);

    *ret = 0;
    int sys_ret = truncate(full_path, newsize);

    if (sys_ret < 0) {
        *ret = -errno;
    }

    free(full_path);

    return 0;
}

int watdfs_fsync(int *argTypes, void **args) {
    char *short_path = (char *)args[0];
    struct fuse_file_info *fi = (struct fuse_file_info *)args[1];
    int *ret = (int *)args[2];
    char *full_path = get_full_path(short_path);

    *ret = 0;
    int sys_ret = fsync(fi->fh);

    if (sys_ret < 0) {
        *ret = -errno;
    }

    free(full_path);

    return 0;
}

int watdfs_utimens(int *argTypes, void **args) {
    char *short_path = (char *)args[0];
    const struct timespec *ts = (struct timespec *)args[1];
    int *ret = (int *)args[2];
    char *full_path = get_full_path(short_path);

    *ret = 0;
    // int sys_ret = futimens(fi->fh, ts);
    int sys_ret = utimensat(0, full_path, ts, 0);

    if (sys_ret < 0) {
        *ret = -errno;
    }

    free(full_path);

    return 0;
}

// The main function of the server.
int main(int argc, char *argv[]) {
    // argv[1] should contain the directory where you should store data on the
    // server. If it is not present it is an error, that we cannot recover from.
    if (argc != 2) {
        // In general you shouldn't print to stderr or stdout, but it may be
        // helpful here for debugging. Important: Make sure you turn off logging
        // prior to submission!
        // See watdfs_client.c for more details
        // # ifdef PRINT_ERR
        // std::cerr << "Usaage:" << argv[0] << " server_persist_dir";
        // #endif
        return -1;
    }
    // Store the directory in a global variable.
    server_persist_dir = argv[1];

    // TODO: Initialize the rpc library by calling `rpcServerInit`.
    // Important: `rpcServerInit` prints the 'export SERVER_ADDRESS' and
    // 'export SERVER_PORT' lines. Make sure you *do not* print anything
    // to *stdout* before calling `rpcServerInit`.
    //DLOG("Initializing server...");

    int ret = rpcServerInit();
    // TODO: If there is an error with `rpcServerInit`, it maybe useful to have
    // debug-printing here, and then you should return.
    if (ret != 0) {
#ifdef PRINT_ERR
        std::cerr << "Failed to initialize RPC Server" << std::endl;
#endif
	return -errno;
    }

    // TODO: Register your functions with the RPC library.
    // Note: The braces are used to limit the scope of `argTypes`, so that you can
    // reuse the variable for multiple registrations. Another way could be to
    // remove the braces and use `argTypes0`, `argTypes1`, etc.
    {
        // There are 3 args for the function (see watdfs_client.c for more
        // detail).
        int argTypes[4];
        // First is the path.
        argTypes[0] =
            (1u << ARG_INPUT) | (1u << ARG_ARRAY) | (ARG_CHAR << 16u) | 1u; // why 1u here????
        // The second argument is the statbuf.
        argTypes[1] =
            (1u << ARG_OUTPUT) | (1u << ARG_ARRAY) | (ARG_CHAR << 16u) | 1u;
        // The third argument is the retcode.
        argTypes[2] = (1u << ARG_OUTPUT) | (ARG_INT << 16u);
        // Finally we fill in the null terminator.
        argTypes[3] = 0;

        // We need to register the function with the types and the name.
        ret = rpcRegister((char *)"getattr", argTypes, watdfs_getattr);
        if (ret < 0) {
            // It may be useful to have debug-printing here.
            return ret;
        }
    }

    {   // mknod
        int argTypes[5];

        argTypes[0] =
            (1u << ARG_INPUT) | (1u << ARG_ARRAY) | (ARG_CHAR << 16u) | 1u;
        argTypes[1] = (1u << ARG_INPUT) | (ARG_INT << 16u);
        argTypes[2] = (1u << ARG_INPUT) | (ARG_LONG << 16u);
        argTypes[3] = (1u << ARG_OUTPUT) | (ARG_INT << 16u);
        argTypes[4] = 0;

        ret = rpcRegister((char *)"mknod", argTypes, watdfs_mknod);
        if (ret < 0) {
            // It may be useful to have debug-printing here.
            return ret;
        }
    }

    {   // open
        int argTypes[4];

        argTypes[0] =
            (1u << ARG_INPUT) | (1u << ARG_ARRAY) | (ARG_CHAR << 16u) | 1u;
        argTypes[1] = (1u << ARG_INPUT) | (1u << ARG_OUTPUT) | (1u << ARG_ARRAY) | (ARG_CHAR << 16u) | 1u;
        argTypes[2] = (1u << ARG_OUTPUT) | (ARG_INT << 16u);
        argTypes[3] = 0;

        ret = rpcRegister((char *)"open", argTypes, watdfs_open);
        if (ret < 0) {
            // It may be useful to have debug-printing here.
            return ret;
        }
    }

    {   // release
        int argTypes[4];

        argTypes[0] =
            (1u << ARG_INPUT) | (1u << ARG_ARRAY) | (ARG_CHAR << 16u) | 1u;
        argTypes[1] = (1u << ARG_INPUT) | (1u << ARG_ARRAY) | (ARG_CHAR << 16u) | 1u;
        argTypes[2] = (1u << ARG_OUTPUT) | (ARG_INT << 16u);
        argTypes[3] = 0;

        ret = rpcRegister((char *)"release", argTypes, watdfs_release);
        if (ret < 0) {
            // It may be useful to have debug-printing here.
            return ret;
        }
    }

    {   // read
        int argTypes[7];

        argTypes[0] =
            (1u << ARG_INPUT) | (1u << ARG_ARRAY) | (ARG_CHAR << 16u) | 1u;
        argTypes[1] = (1u << ARG_OUTPUT) | (1u << ARG_ARRAY) | (ARG_CHAR << 16u) | 1u;
        argTypes[2] = (1u << ARG_INPUT) | (ARG_LONG << 16u);
        argTypes[3] = (1u << ARG_INPUT) | (ARG_LONG << 16u);
        argTypes[4] = (1u << ARG_INPUT) | (1u << ARG_ARRAY) | (ARG_CHAR << 16u) | 1u;
        argTypes[5] = (1u << ARG_OUTPUT) | (ARG_INT << 16u);
        argTypes[6] = 0;

        ret = rpcRegister((char *)"read", argTypes, watdfs_read);
        if (ret < 0) {
            // It may be useful to have debug-printing here.
            return ret;
        }
    }

    {   // write
        int argTypes[7];

        argTypes[0] = (1u << ARG_INPUT) | (1u << ARG_ARRAY) | (ARG_CHAR << 16u) | 1u;
        argTypes[1] = (1u << ARG_INPUT) | (1u << ARG_ARRAY) | (ARG_CHAR << 16u) | 1u;
        argTypes[2] = (1u << ARG_INPUT) | (ARG_LONG << 16u);
        argTypes[3] = (1u << ARG_INPUT) | (ARG_LONG << 16u);
        argTypes[4] = (1u << ARG_INPUT) | (1u << ARG_ARRAY) | (ARG_CHAR << 16u) | 1u;
        argTypes[5] = (1u << ARG_OUTPUT) | (ARG_INT << 16u);
        argTypes[6] = 0;

        ret = rpcRegister((char *)"write", argTypes, watdfs_write);
        if (ret < 0) {
            // It may be useful to have debug-printing here.
            return ret;
        }
    }

    {   // truncate
        int argTypes[4];

        argTypes[0] =
            (1u << ARG_INPUT) | (1u << ARG_ARRAY) | (ARG_CHAR << 16u) | 1u;
        argTypes[1] = (1u << ARG_INPUT) | (ARG_LONG << 16u);
        argTypes[2] = (1u << ARG_OUTPUT) | (ARG_INT << 16u);
        argTypes[3] = 0;

        ret = rpcRegister((char *)"truncate", argTypes, watdfs_truncate);
        if (ret < 0) {
            // It may be useful to have debug-printing here.
            return ret;
        }
    }

    {   // fsync
        int argTypes[4];

        argTypes[0] =
            (1u << ARG_INPUT) | (1u << ARG_ARRAY) | (ARG_CHAR << 16u) | 1u;
        argTypes[1] = (1u << ARG_INPUT) | (1u << ARG_ARRAY) | (ARG_CHAR << 16u) | 1u;
        argTypes[2] = (1u << ARG_OUTPUT) | (ARG_INT << 16u);
        argTypes[3] = 0;

        ret = rpcRegister((char *)"fsync", argTypes, watdfs_fsync);
        if (ret < 0) {
            // It may be useful to have debug-printing here.
            return ret;
        }
    }

    {   // utimens
        int argTypes[4];

        argTypes[0] =
            (1u << ARG_INPUT) | (1u << ARG_ARRAY) | (ARG_CHAR << 16u) | 1u;
        argTypes[1] = (1u << ARG_INPUT) | (1u << ARG_ARRAY) | (ARG_CHAR << 16u) | 1u;
        argTypes[2] = (1u << ARG_OUTPUT) | (ARG_INT << 16u);
        argTypes[3] = 0;

        ret = rpcRegister((char *)"utimens", argTypes, watdfs_utimens);
        if (ret < 0) {
            // It may be useful to have debug-printing here.
            return ret;
        }
    }

    {   // lock
        int argTypes[4];

        argTypes[0] =
            (1u << ARG_INPUT) | (1u << ARG_ARRAY) | (ARG_CHAR << 16u) | 1u;
        argTypes[1] = (1u << ARG_INPUT) | (ARG_INT << 16u);
        argTypes[2] = (1u << ARG_OUTPUT) | (ARG_INT << 16u);
        argTypes[3] = 0;

        ret = rpcRegister((char *)"lock", argTypes, watdfs_lock);
        if (ret < 0) {
            // It may be useful to have debug-printing here.
            return ret;
        }
    }

    {   // unlock
        int argTypes[4];

        argTypes[0] =
            (1u << ARG_INPUT) | (1u << ARG_ARRAY) | (ARG_CHAR << 16u) | 1u;
        argTypes[1] = (1u << ARG_INPUT) | (ARG_INT << 16u);
        argTypes[2] = (1u << ARG_OUTPUT) | (ARG_INT << 16u);
        argTypes[3] = 0;

        ret = rpcRegister((char *)"unlock", argTypes, watdfs_unlock);
        if (ret < 0) {
            // It may be useful to have debug-printing here.
            return ret;
        }
    }

    // TODO: Hand over control to the RPC library by calling `rpcExecute`.
    ret = rpcExecute();
    // rpcExecute could fail so you may want to have debug-printing here, and
    // then you should return.
    if (ret < 0) {
#ifdef PRINT_ERR
        std::cerr << "Failed to Execute RPC Server" << std::endl;
#endif
    }

    std::map<std::string, struct file_info *>::iterator file;
    for (file = global_file_state.begin(); file != global_file_state.end(); file ++) {
        int lock_ret = rw_lock_destroy(file->second->lock);
        if (lock_ret < 0) {
            ret = lock_ret;
        }

        delete file->second;
    }

    return ret;
}
