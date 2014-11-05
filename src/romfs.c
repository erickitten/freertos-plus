#include <string.h>
#include <FreeRTOS.h>
#include <semphr.h>
#include <unistd.h>
#include "fio.h"
#include "filesystem.h"
#include "romfs.h"
#include "osdebug.h"
#include "hash-djb2.h"

//file handle unique to romfs
struct romfs_fds_t {
    const uint8_t * file;
	uint32_t size;
    uint32_t cursor;
};

//opened file handles unique to romfs
static struct romfs_fds_t romfs_fds[MAX_FDS];

static uint32_t get_unaligned(const uint8_t * d) {
    return ((uint32_t) d[0]) | ((uint32_t) (d[1] << 8)) | ((uint32_t) (d[2] << 16)) | ((uint32_t) (d[3] << 24));
}

static ssize_t romfs_read(void * opaque, void * buf, size_t count) {
    struct romfs_fds_t * f = (struct romfs_fds_t *) opaque;
    
    if ((f->cursor + count) > f->size)
        count = f->size - f->cursor;

    memcpy(buf, f->file + f->cursor, count);
    f->cursor += count;

    return count;
}

static off_t romfs_seek(void * opaque, off_t offset, int whence) {
    struct romfs_fds_t * f = (struct romfs_fds_t *) opaque;
    uint32_t origin;
    
    switch (whence) {
    case SEEK_SET:
        origin = 0;
        break;
    case SEEK_CUR:
        origin = f->cursor;
        break;
    case SEEK_END:
        origin = f->size;
        break;
    default:
        return -1;
    }

    offset = origin + offset;

    if (offset < 0)
        return -1;
    if (offset > f->size)
        offset = f->size;

    f->cursor = offset;

    return offset;
}

const uint8_t * romfs_get_handle(const uint8_t * romfs, const char * path,int finddir) {
	const uint8_t * meta;
	const char *slash;

	while(*path =='/'){
		path++;
	}

	//if there is more slash ,search the directory
	//else ,search for file handle
	slash = strchr(path, '/');

	for(meta = romfs;;meta += (get_unaligned(meta) & 0xfffffff0)){
		if((!slash) && strcmp((const char*)meta+16,path) == 0){
			//file found
			if((get_unaligned(meta) & 0x0000000f) != 0x2){//not a regular file
				if(finddir){
					return meta;
				}else{
					return NULL;
				}
			}
			return meta;
		}else if((slash != NULL) && (!strncmp(path,(const char*)meta+16,slash - path)) && 
				(meta+16)[slash - path] == '\0'){
			//directory found ,recursive call ,directory location as entry point
			meta+=4;
			return romfs_get_handle(meta + get_unaligned(meta),slash,0);
		}
		
		//end of directory
		if(!(get_unaligned(meta) >> 4)){
			break;
		}
	}
	return NULL;
}

const uint8_t * romfs_get_file_by_name(const uint8_t * romfs, const char * path, uint32_t * len){

	const uint8_t * meta = (const uint8_t * )romfs_get_handle(romfs,path,0);
	if(!meta){
		return NULL;
	}
	if(len){
		*len = get_unaligned(meta+8);
	}
	meta += 16;
	//name length is not written in romfs
	//look for NULL and go to closest next 16byte
	while(*(meta+15) != '\0'){
		meta += 16;
	}
	return meta+16;
}

const char* romfs_readdir(void * opaque,const char * path){
	static uint8_t * ptr;

	if(path == NULL){
		if((get_unaligned(ptr) & 0xfffffff0) == 0){
			return NULL;
		}
		ptr += (get_unaligned(ptr) & 0xfffffff0);
	}else{
		 ptr = romfs_get_handle(opaque,path,1);
	}

	return (ptr+16);
}


static int romfs_open(void * opaque, const char * path, int flags, int mode) {
	const uint8_t * romfs = (const uint8_t *) opaque;
	const uint8_t * file;
	uint32_t fsize;
	int r = -1;

	file = romfs_get_file_by_name(romfs, path, &fsize);

	if (file) {
		r = fio_open(romfs_read, NULL, romfs_seek, NULL, NULL);
		if (r > 0) {
			//the emptyness of file handle index (r) has been checked in fio_open
			//thus , there is no need to check if romfs_fds[r] is empty
			romfs_fds[r].file = file;
			romfs_fds[r].cursor = 0;
			romfs_fds[r].size = fsize;
			fio_set_opaque(r, romfs_fds + r);
		}
	}
	return r;
}

void register_romfs(const char * mountpoint, const uint8_t * romfs) {
//	DBGOUT("Registering romfs `%s' @ %p\r\n", mountpoint, romfs);
	register_fs(mountpoint, romfs_open,romfs_readdir, (void *) romfs);
}
