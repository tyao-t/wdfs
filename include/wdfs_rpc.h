#ifndef WATDFS_RPC_H
#define WATDFS_RPC_H

// The FUSE API has been changed a number of times.  Our code needs to define
// the version of the API that we assume.  As of this writing, a stable API
// version is 26.
#define FUSE_USE_VERSION 26

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fuse.h>
#include <libgen.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include "rw_lock.h"

#ifdef __cplusplus
// This is for backward compatibility, but all your code *must* be in C++.
extern "C" {
#endif

// SETUP AND TEARDOWN
void *watdfs_rpc_init(struct fuse_conn_info *conn, const char *path_to_cache,
                      time_t cache_interval, int *retcode);
void watdfs_rpc_destroy(void *userdata);

// GET FILE ATTRIBUTES
int watdfs_rpc_getattr(void *userdata, const char *path, struct stat *statbuf);

// CREATE, OPEN AND CLOSE
int watdfs_rpc_mknod(void *userdata, const char *path, mode_t mode, dev_t dev);
int watdfs_rpc_open(void *userdata, const char *path,
                    struct fuse_file_info *fi);
int watdfs_rpc_release(void *userdata, const char *path,
                       struct fuse_file_info *fi);

// READ AND WRITE DATA
int watdfs_rpc_read(void *userdata, const char *path, char *buf, size_t size,
                    off_t offset, struct fuse_file_info *fi);
int watdfs_rpc_write(void *userdata, const char *path, const char *buf,
                     size_t size, off_t offset, struct fuse_file_info *fi);
int watdfs_rpc_truncate(void *userdata, const char *path, off_t newsize);
int watdfs_rpc_fsync(void *userdata, const char *path,
                     struct fuse_file_info *fi);

// CHANGE METADATA
int watdfs_rpc_utimens(void *userdata, const char *path,
                       const struct timespec ts[2]);

// Atomicity
int watdfs_rpc_lock(const char *path, rw_lock_mode_t mode);
int watdfs_rpc_unlock(const char *path, rw_lock_mode_t mode);

#ifdef __cplusplus
}
#endif

#endif
