#ifdef _WIN32
#include <windows.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <dirent.h>
#include <string.h>

#define hash_init 5381

uint32_t hash_djb2(const uint8_t * str, uint32_t hash) {
    int c;

    while ((c = *str++))
        hash = ((hash << 5) + hash) ^ c;

    return hash;
}

void usage(const char * binname) {
    printf("Usage: %s [-d <dir>] [outfile]\n", binname);
    exit(-1);
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
    uint32_t size, w ,namesize ,nextf ,i;
    uint8_t b;
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

			i = ftell(outfile);
			//placeholder for offset after recursive call is done
			b = 0;fwrite(&b, 4, 1, outfile);
			
			//write spec.info ,which is first file header in directory
			b = (12 >>  0) & 0xff; fwrite(&b, 1, 1, outfile);
            b = (12 >>  8) & 0xff; fwrite(&b, 1, 1, outfile);
            b = (12 >> 16) & 0xff; fwrite(&b, 1, 1, outfile);
            b = (12 >> 24) & 0xff; fwrite(&b, 1, 1, outfile);

			//write size = checksum =0
			b = 0;fwrite(&b, 4, 1, outfile);
			fwrite(&b, 4, 1, outfile);

            strcat(fullpath, "/");
            //if is directory ,recursive call processdir
            rec_dirp = opendir(fullpath);
            processdir(rec_dirp, fullpath + strlen(prefix) + 1, outfile, prefix);
            //after this is done ,go back to write next filehdr
			w = ftell(outfile);
			nextf = ((w-i) & 0xfffffff0) | 0x1;//mapping 0x1 = dir
			fseek(outfile,i,SEEK_SET);

			b = (nextf >>  0) & 0xff; fwrite(&b, 1, 1, outfile);
            b = (nextf >>  8) & 0xff; fwrite(&b, 1, 1, outfile);
            b = (nextf >> 16) & 0xff; fwrite(&b, 1, 1, outfile);
            b = (nextf >> 24) & 0xff; fwrite(&b, 1, 1, outfile);

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
			nextf = nextf |= 0x00000002;
			//write nextf
            b = (nextf >>  0) & 0xff; fwrite(&b, 1, 1, outfile);
            b = (nextf >>  8) & 0xff; fwrite(&b, 1, 1, outfile);
            b = (nextf >> 16) & 0xff; fwrite(&b, 1, 1, outfile);
            b = (nextf >> 24) & 0xff; fwrite(&b, 1, 1, outfile);

			//write spec.info ,which is 0 for file
			b = 0;fwrite(&b, 4, 1, outfile);

            //write file size
            b = (size >>  0) & 0xff; fwrite(&b, 1, 1, outfile);
            b = (size >>  8) & 0xff; fwrite(&b, 1, 1, outfile);
            b = (size >> 16) & 0xff; fwrite(&b, 1, 1, outfile);
            b = (size >> 24) & 0xff; fwrite(&b, 1, 1, outfile);

			//write checksum (not implemented)
            b = 0;fwrite(&b, 4, 1, outfile);

			//write file name
			fwrite(ent->d_name,1,i,outfile);
			b = '\0';
			for(;namesize - i>0;i++){
				fwrite(&b,1,1,outfile);
			}

            //write the file
            while (size) {
                w = size > 16 * 1024 ? 16 * 1024 : size;
                fread(buf, 1, w, infile);
                fwrite(buf, 1, w, outfile);
                size -= w;
            }
//TODO : PAD TO 16 BYTE FOR CONTENT
            fclose(infile);
        }
    }
}

int main(int argc, char ** argv) {
    char * binname = *argv++;	//this program
    char * o;
    char * outname = NULL;
    char * dirname = ".";	//input dir name ,current if none given
    uint64_t z = 0;
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
    fwrite(&z, 1, 8, outfile);
    if (outname)
        fclose(outfile);
    closedir(dirp);
    
    return 0;
}
