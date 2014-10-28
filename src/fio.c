#include <string.h>
#include <FreeRTOS.h>
#include <semphr.h>
#include <unistd.h>
#include "fio.h"
#include "filesystem.h"
#include "osdebug.h"
#include "hash-djb2.h"

//handles of opened files
static struct fddef_t fio_fds[MAX_FDS];

/* recv_byte is define in main.c */
char recv_byte();
void send_byte(char);

enum KeyName{ESC=27, BACKSPACE=127};


/*
stdio functions

for reading & writing through serial interface
 */

//@TODO need futher check
static ssize_t stdin_read(void * opaque, void * buf, size_t count) {
	int i=0, endofline=0, last_chr_is_esc;
	char *ptrbuf=buf;
	char ch;
	while(i < count&&endofline!=1){
		ptrbuf[i]=recv_byte();
		switch(ptrbuf[i]){
		case '\r':
		case '\n':
			ptrbuf[i]='\0';
			endofline=1;
			break;
		case '[':
			if(last_chr_is_esc){
				last_chr_is_esc=0;
				ch=recv_byte();
				if(ch>=1&&ch<=6){
					ch=recv_byte();
				}
				continue;
			}
			break;
		case ESC:
			last_chr_is_esc=1;
			continue;
		case BACKSPACE:
			last_chr_is_esc=0;
			if(i>0){
				send_byte('\b');
				send_byte(' ');
				send_byte('\b');
				--i;
			}
			continue;
		default:
			last_chr_is_esc=0;
			break;
		}
		send_byte(ptrbuf[i]);
		++i;
	}
	return i;
}

static ssize_t stdout_write(void * opaque, const void * buf, size_t count) {
	int i;
	const char * data = (const char *) buf;
    
	for (i = 0; i < count; i++){
		send_byte(data[i]);
	}   
	return count;
}


/*
open file functions
*/

static xSemaphoreHandle fio_sem = NULL;

//init & set stdio to fio_fds
__attribute__((constructor)) void fio_init() {
    memset(fio_fds, 0, sizeof(fio_fds));
    fio_fds[0].fdread = stdin_read;
    fio_fds[1].fdwrite = stdout_write;
    fio_fds[2].fdwrite = stdout_write;
    fio_sem = xSemaphoreCreateMutex();
}

//check if given file handle is open
static int fio_is_open_int(int fd) {
	if ((fd < 0) || (fd >= MAX_FDS)){//bound check
		return 0;
	}
    int r = !((fio_fds[fd].fdread == NULL) &&
	      (fio_fds[fd].fdwrite == NULL) &&
	      (fio_fds[fd].fdseek == NULL) &&
	      (fio_fds[fd].fdclose == NULL) &&
	      (fio_fds[fd].opaque == NULL));
    return r;
}

//search for empty space in fio_fds to open to
static int fio_findfd() {
	int i;
    
	for (i = 0; i < MAX_FDS; i++) {
		if (!fio_is_open_int(i)){
			return i;
		}
	}	
	
	//since this function is called by fio_open
	//this make sense in this context
	return OPENFAIL;
}

int fio_is_open(int fd) {
    int r = 0;
    xSemaphoreTake(fio_sem, portMAX_DELAY);
    r = fio_is_open_int(fd);
    xSemaphoreGive(fio_sem);
    return r;
}

//open a file to an empty space in fio_fds
//@return index of said space ,as file handle
int fio_open(fdread_t fdread, fdwrite_t fdwrite, fdseek_t fdseek, fdclose_t fdclose, void * opaque) {
    int fd;
//    DBGOUT("fio_open(%p, %p, %p, %p, %p)\r\n", fdread, fdwrite, fdseek, fdclose, opaque);
    xSemaphoreTake(fio_sem, portMAX_DELAY);
    fd = fio_findfd();
    
    if (fd >= 0) {
	fio_fds[fd].fdread = fdread;
	fio_fds[fd].fdwrite = fdwrite;
	fio_fds[fd].fdseek = fdseek;
	fio_fds[fd].fdclose = fdclose;
	fio_fds[fd].opaque = opaque;
    }
    xSemaphoreGive(fio_sem);
    
    return fd;
}


/*
file IO interface

the file handle (fd) is an index to fio_fds[]
returned by fio_open()
which is a newly filled handle found by fio_findfd
*/

ssize_t fio_read(int fd, void * buf, size_t count) {
    ssize_t r = 0;
//    DBGOUT("fio_read(%i, %p, %i)\r\n", fd, buf, count);
	if (fio_is_open_int(fd)) {
		if (fio_fds[fd].fdread) {
			r = fio_fds[fd].fdread(fio_fds[fd].opaque, buf, count);
		} else {
			r = -3;
		}
	} else {
		r = -2;
	}
	return r;
}

ssize_t fio_write(int fd, const void * buf, size_t count) {
	ssize_t r = 0;
//	DBGOUT("fio_write(%i, %p, %i)\r\n", fd, buf, count);
	if (fio_is_open_int(fd)) {
		if (fio_fds[fd].fdwrite) {
			r = fio_fds[fd].fdwrite(fio_fds[fd].opaque, buf, count);
		} else {
			r = -3;
		}
	} else {
		r = -2;
	}
	return r;
}

off_t fio_seek(int fd, off_t offset, int whence) {
    off_t r = 0;
//    DBGOUT("fio_seek(%i, %i, %i)\r\n", fd, offset, whence);
	if (fio_is_open_int(fd)) {
		if (fio_fds[fd].fdseek) {
			r = fio_fds[fd].fdseek(fio_fds[fd].opaque, offset, whence);
		} else {
			r = -3;
		}
	} else {
		r = -2;
	}
	return r;
}

int fio_close(int fd) {
	int r = 0;
//	DBGOUT("fio_close(%i)\r\n", fd);
	if (fio_is_open_int(fd)) {
		if (fio_fds[fd].fdclose){
			r = fio_fds[fd].fdclose(fio_fds[fd].opaque);
		}
		xSemaphoreTake(fio_sem, portMAX_DELAY);
		memset(fio_fds + fd, 0, sizeof(struct fddef_t));
		xSemaphoreGive(fio_sem);
	} else {
		r = -2;
	}
	return r;
}

void fio_set_opaque(int fd, void * opaque) {
	if (fio_is_open_int(fd)){
		fio_fds[fd].opaque = opaque;
	}
}


/*
devfs (stdio) filesystem
*/
#define stdin_hash 0x0BA00421
#define stdout_hash 0x7FA08308
#define stderr_hash 0x7FA058A3

//open (alternate) handle of stdio
//since fio_init have already init them (fio_fds[0~2])
static int devfs_open(void * opaque, const char * path, int flags, int mode) {
	uint32_t h = hash_djb2((const uint8_t *) path, -1);

//    DBGOUT("devfs_open(%p, \"%s\", %i, %i)\r\n", opaque, path, flags, mode);

	switch (h) {
	case stdin_hash:
		if (flags & (O_WRONLY | O_RDWR)){
			return OPENFAIL;
		}
		return fio_open(stdin_read, NULL, NULL, NULL, NULL);
		break;
    	case stdout_hash:
		if (flags & O_RDONLY){
			return OPENFAIL;
		}
		return fio_open(NULL, stdout_write, NULL, NULL, NULL);
		break;
	case stderr_hash:
		if (flags & O_RDONLY){
			return OPENFAIL;
		}
		return fio_open(NULL, stdout_write, NULL, NULL, NULL);
		break;
	}
	return OPENFAIL;
}

//it is never actually registered as far as I can tell
void register_devfs() {
//	DBGOUT("Registering devfs.\r\n");
	register_fs("dev", devfs_open, NULL);
}
