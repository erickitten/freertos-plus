#ifndef __FILESYSTEM_H__
#define __FILESYSTEM_H__

#include <stdint.h>
#include <hash-djb2.h>

#define MAX_FS 16
#define OPENFAIL (-1)


typedef int (*fs_open_t)(void * opaque, const char * fname, int flags, int mode);
typedef const char* (*fs_read_t)(void * opaque, const char *path);

/* Need to be called before using any other fs functions */
__attribute__((constructor)) void fs_init();

int register_fs(const char * mountpoint, fs_open_t callback,fs_read_t readdir, void * opaque);
int fs_open(const char * path, int flags, int mode);
const char* fs_readdir(const char * path);

#endif
