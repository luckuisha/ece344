#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include "common.h"

void usage();
void copyFile(char * Spath, char * Dpath);
void copyFolder (DIR * currentDirectory, char * source, char * dest);
void copy (char * source, char * dest);
/* make sure to use syserror() when a system call fails. see common.h */

void
usage()
{
	fprintf(stderr, "Usage: cpr srcdir dstdir\n");
	exit(1);
}

int
main(int argc, char *argv[])
{
	if (argc != 3) {
		usage();
	}
	copy(argv[1], argv[2]);
	
	return 0;
}

void copy (	char * source, char * dest){
	//check if path is file or folder 
	struct stat s;
	DIR * Sdir; //holds the address to the source directory
	if( stat(source,&s) == 0 )
	{
		if( S_ISDIR(s.st_mode) )
		{
			//open source directory
			Sdir = opendir(source);  
			if (Sdir == NULL){
				syserror(opendir, source);
			}

			copyFolder(Sdir, source, dest);
		}
		else if( S_ISREG(s.st_mode) )
		{
			copyFile(source, dest);
		}
		else
		{
			syserror(open, source);
		}
	}
	return;
}

void copyFolder(DIR * currentSrcDirectory, char * source, char * dest){
	struct dirent * dir;
	struct stat s;
	
	DIR * nextSrcDirectory;
	
	stat(dest, &s);
	

	if (mkdir(dest, 0755) < 0) {
		syserror(mkdir, dest);
	};

	char srcPath[4096];
	char destPath[4096];
	while ((dir = readdir(currentSrcDirectory)) != NULL)
	{
		if(strcmp(dir->d_name,".")!=0 && strcmp(dir->d_name,"..")!=0){
			strcpy(srcPath, source);
			strcat(srcPath, "/");
			strcat(srcPath, dir->d_name);
			strcpy(destPath, dest);
			strcat(destPath, "/");
			strcat(destPath, dir->d_name);
			
			if(dir-> d_type != DT_DIR){ //if not folder
				copyFile(srcPath, destPath);
			}
			else if(dir -> d_type == DT_DIR ){ // if it is a directory
				nextSrcDirectory = opendir(srcPath);
				if (nextSrcDirectory == NULL){
					syserror(opendir, srcPath);
				}
				
				copyFolder(nextSrcDirectory, srcPath, destPath);
				
				
			}
		}
	}
	
	
	chmod(dest, s.st_mode);
	closedir(currentSrcDirectory);
	return;

}

void copyFile(char * Spath, char * Dpath){
	int Sfd, Dfd; //source and destination file description respectivly
	int Rbyt, Wbyt; //read and write bytes respectivly
	char buff[4096]; //buffer
	struct stat s;

	stat(Spath, &s);

    Sfd = open(Spath, O_RDONLY);
	if (Sfd < 0){
		syserror(open, Spath);
	}
	Dfd = creat(Dpath, O_RDWR);
	if (Dfd < 0 ){
		syserror(creat, Dpath);
	}

    while (1) {
        Rbyt = read(Sfd, buff, 4096);
        if (Rbyt < 0) {
            syserror(read, Spath);
        }

        if (Rbyt == 0){
			break;
		}

        Wbyt = write(Dfd, buff, Rbyt);
        if (Wbyt < 0) {
            syserror(write, Dpath);
        }
    }

	chmod(Dpath, s.st_mode);

    close(Sfd);
	return;
}