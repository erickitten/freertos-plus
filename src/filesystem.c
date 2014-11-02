#include "osdebug.h"
#include "filesystem.h"
#include "fio.h"

#include <stdint.h>
#include <string.h>
#include <hash-djb2.h>

#define MAX_FS 16

//file system handle
struct fs_t {
	const char * path;
	fs_open_t cb;
	void * opaque;
};

//registered file system handles
static struct fs_t fss[MAX_FS];

__attribute__((constructor)) void fs_init() {
    memset(fss, 0, sizeof(fss));
}

int register_fs(const char * mountpoint, fs_open_t callback, void * opaque) {
	int i;
//	DBGOUT("register_fs(\"%s\", %p, %p)\r\n", mountpoint, callback, opaque);
    
	for (i = 0; i < MAX_FS; i++) {
		if (!fss[i].cb) {
			fss[i].path = mountpoint;//shallow copy
			fss[i].cb = callback;
			fss[i].opaque = opaque;
			return 0;
		}
	}
    
	return -1;
}

int fs_open(const char * path, int flags, int mode) {
	const char * slash;
	const char * fsn;
	int i;

//	DBGOUT("fs_open(\"%s\", %i, %i)\r\n", path, flags, mode);
    
	while (path[0] == '/'){
		path++;
	}
    
	fsn = path;
	slash = strchr(path, '/');
    
	if (!slash){
		return OPENFAIL;
	}

	// @ /[fs_path]/[file path]
	path = slash + 1;

	for (i = 0; i < MAX_FS; i++) {
		if (!strncmp(fss[i].path,fsn,slash - fsn) && 
				fss[i].path[slash-fsn] == '\0'){
			//compare [fs_path] & make sure length is same
			return fss[i].cb(fss[i].opaque, path, flags, mode);
		}
	}
	    
	return OPENFAIL;
}
