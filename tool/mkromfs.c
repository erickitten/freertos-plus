#ifdef _WIN32
#include <windows.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <dirent.h>
#include <string.h>


void usage(const char * binname) {
    printf("Usage: %s [-d <dir>] [outfile]\n", binname);
    exit(-1);
}

void write_unaligned(int i,FILE * fp){
	uint8_t b = 0;
	b = (i >>  0) & 0xff; fwrite(&b, 1, 1, fp);
 	b = (i >>  8) & 0xff; fwrite(&b, 1, 1, fp);
 	b = (i >> 16) & 0xff; fwrite(&b, 1, 1, fp);
 	b = (i >> 24) & 0xff; fwrite(&b, 1, 1, fp);
}

void write_repeative(uint8_t byte,int time,FILE *fp){
	int i;
	for(i=0;i < time;i++){
		fwrite(&byte,1,1,fp);
	}
}

/*
@DIR : the current dir to be processed
@curpath : current dir name to be processed
@outfile : output object file
@prefix : the starting dir of processing (input dir name)
*/
void processdir(DIR * dirp, const char * curpath, FILE * outfile, const char * prefix) {
    char fullpath[1024];
    char buf[16 * 1024];
    struct dirent * ent;
    DIR * rec_dirp;
    uint32_t size, w ,namesize ,nextf ,i ,floc;
    FILE * infile;

    while ((ent = readdir(dirp))) {
        strcpy(fullpath, prefix);
        strcat(fullpath, "/");
        strcat(fullpath, curpath);
        strcat(fullpath, ent->d_name);
    #ifdef _WIN32
        if (GetFileAttributes(fullpath) & FILE_ATTRIBUTE_DIRECTORY) {
    #else
        if (ent->d_type == DT_DIR) {
    #endif
            //ignore . & ..
            if (strcmp(ent->d_name, ".") == 0)
                continue;
            if (strcmp(ent->d_name, "..") == 0)
                continue;
			
			//file size ,padded to 16 byte
			namesize = (( (i = strlen(ent->d_name)) +1)+16) & 0xfffffff0;

			floc = ftell(outfile);
			//placeholder for offset after recursive call is done
			write_repeative(0, 4, outfile);
			
			//write spec.info ,which is first file header in directory
			write_unaligned(12,outfile);

			//write size = checksum =0
			write_repeative(0,8,outfile);

			fwrite(ent->d_name,1,i,outfile);
            write_repeative(0,namesize-i,outfile);

            strcat(fullpath, "/");
            //if is directory ,recursive call processdir
            rec_dirp = opendir(fullpath);
            processdir(rec_dirp, fullpath + strlen(prefix) + 1, outfile, prefix);
            //after this is done ,go back to write next filehdr
			w = ftell(outfile);
			nextf = ((w-floc) & 0xfffffff0) | 0x1;//mapping 0x1 = dir
			fseek(outfile,floc,SEEK_SET);

			write_unaligned(nextf,outfile);

			fseek(outfile,w,SEEK_SET);//return to original position

			closedir(rec_dirp);
        } else {
            infile = fopen(fullpath, "rb");
            if (!infile) {
                perror("opening input file");
                exit(-1);
            }
			//find file size
            fseek(infile, 0, SEEK_END);
            size = ftell(infile);
            fseek(infile, 0, SEEK_SET);
			
			//padded to 16 byte boundary (including null)
			//((n/16) +1)*16
            namesize = (( (i = strlen(ent->d_name)) +1)+16) & 0xfffffff0;

			//next filehdr
            //find next file offset (also at 16 byte boundry)
			nextf = ((16/*header*/+namesize+size)+16) &  0xfffffff0;
			//add file mapping (regular file 0x2)
			nextf = nextf | 0x00000002;

			floc = ftell(outfile);
			//write nextf
			write_unaligned(nextf,outfile);

			//write spec.info ,which is 0 for file
			write_repeative(0,4,outfile);

            //write file size
			write_unaligned(size,outfile);

			//write checksum (not implemented ,write 0)
			write_repeative(0,4,outfile);

			//write file name ,i == strlen(ent->d_name)
			fwrite(ent->d_name,1,i,outfile);
			write_repeative(0,namesize-i,outfile);

			//required padding for content
			i = (nextf & 0xfffffff0) - (size+namesize+16);
            //write the file
            while (size) {
                w = size > 16 * 1024 ? 16 * 1024 : size;
                fread(buf, 1, w, infile);
                fwrite(buf, 1, w, outfile);
                size -= w;
            }
			//pad to 16 byte for content
			write_repeative(0,i,outfile);
			
            fclose(infile);
        }
    }
	
	//no more file ,modify last file's nextf = 0
	fseek(outfile,floc,SEEK_SET);
	nextf &= 0x0000000f;//leave file mapping info there
	write_unaligned(nextf,outfile);
	fseek(outfile,0,SEEK_END);
}

int main(int argc, char ** argv) {
    char * binname = *argv++;	//this program
    char * o;
    char * outname = NULL;
    char * dirname = ".";	//input dir name ,current if none given
    FILE * outfile;
    DIR * dirp;

    while ((o = *argv++)) {
        if (*o == '-') {
            o++;
            switch (*o) {
            case 'd':
                dirname = *argv++;
                break;
            default:
                usage(binname);
                break;
            }
        } else {
            if (outname)
                usage(binname);
            outname = o;
        }
    }

    if (!outname)
        outfile = stdout;
    else
        outfile = fopen(outname, "wb");

    if (!outfile) {
        perror("opening output file");
        exit(-1);
    }

    dirp = opendir(dirname);
    if (!dirp) {
        perror("opening directory");
        exit(-1);
    }

    processdir(dirp, "", outfile, dirname);
    if (outname)
        fclose(outfile);
    closedir(dirp);
    
    return 0;
}
