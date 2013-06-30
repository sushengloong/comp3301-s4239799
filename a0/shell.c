/*
 * Su Sheng Loong 42397997
 * COMP3301 Assignment 0
 */
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#define BUF_SIZE 4096
#define MAX_CHAR 1024

/*
 * A structure which holds data about a directory entry.
 */
struct ldirent {
	long ino;
	off_t off;
	unsigned short reclen;
	char name[];
};

/*
 * This function reset the count to zero and clear string stored in
 * the command and arguments.
 */
void clear(int *count, char *command, char *arg1, char *arg2)
{
	*count = 0;
	strcpy(command, "");
	strcpy(arg1, "");
	strcpy(arg2, "");
}

/*
 * This function parse the line entered by the user to several tokens,
 * including command, argument 1 and argument 2. The number of arguments
 * including the command will be counted and updated.
 */
void parseline(char *line, int *count, char *command, char *arg1,
	       char *arg2)
{
	char templine[strlen(line) + 1];
	strcpy(templine, line);
	char *ptr = strtok(templine, " ");
	if (ptr != NULL) {
		strcpy(command, ptr);
		ptr = strtok(NULL, " ");
		(*count)++;
	}
	if (ptr != NULL) {
		strcpy(arg1, ptr);
		ptr = strtok(NULL, " ");
		(*count)++;
	}
	if (ptr != NULL) {
		strcpy(arg2, ptr);
		ptr = strtok(NULL, " ");
		(*count)++;
	}
	while (ptr != NULL) {
		(*count)++;
		ptr = strtok(NULL, " ");
	}
}

/*
 * This functions list information about a file or files contained
 * in a directory. Information such as file type, file size and
 * file name will be displayed. The function is able to handle either
 * absolute or relative path by performing some string manipulations.
 */
void ls(int count, char *arg)
{
	if (count > 2) {
		fprintf(stderr, "too many arguments\n");
		return;
	}
	if (count > 1) {
		struct stat argstat;
		if (lstat(arg, &argstat) == -1) {
			perror("lstat");
			return;
		}
		if ((argstat.st_mode & S_IFMT) != S_IFDIR) {
			char argtype;
			switch (argstat.st_mode & S_IFMT) {
			case S_IFREG:
				argtype = 'f';
				break;
			case S_IFLNK:
				argtype = 's';
				break;
			default:
				argtype = 'o';
				break;
			}
			char *lastslash = strrchr(arg, '/');
			printf("%c %10lld %s\n", argtype,
			       (long long) argstat.st_size,
			       (lastslash != NULL) ? ++lastslash : arg);
			return;
		}
	}
	int fd, numread;
	char buffer[BUF_SIZE];
	struct ldirent *d;

	fd = open(count > 1 ? arg : ".", O_RDONLY);
	if (fd == -1) {
		perror("open");
		return;
	}
	do {
		numread = syscall(SYS_getdents, fd, buffer, BUF_SIZE);
		if (numread == -1) {
			perror("getdents");
			return;
		}

		if (numread == 0)
			break;

		int bufferptr;
		for (bufferptr = 0; bufferptr < numread;
		     bufferptr += d->reclen) {
			d = (struct ldirent *) (buffer + bufferptr);
			char *dename = (char *) d->name;
			if (strcmp(dename, ".") == 0
			    || strcmp(dename, "..") == 0)
				continue;
			struct stat fstat;
			/* String manipulation for forming fullpath */
			char fullpath[MAX_CHAR];
			if (count > 1) {
				strcpy(fullpath, arg);
				if (fullpath[strlen(fullpath) - 1] != '/')
					strcat(fullpath, "/");
				strcat(fullpath, dename);
			} else
				strcpy(fullpath, dename);
			if (lstat(fullpath, &fstat) == -1) {
				perror("lstat");
				return;
			}
			char type;
			switch (fstat.st_mode & S_IFMT) {
			case S_IFREG:
				type = 'f';
				break;
			case S_IFLNK:
				type = 's';
				break;
			case S_IFDIR:
				type = 'd';
				break;
			default:
				type = 'o';
				break;
			}
			printf("%c %10lld %s\n", type,
			       (long long) fstat.st_size, dename);
		}
	} while ((numread != 0));
	if (close(fd) == -1) {
		perror("close");
		return;
	}
}

/*
 * A function which copy file from the source path to the destination path.
 * Assumption has been made that both source and destination file must be
 * of regular file type, should the file exists. If the destination file
 * does not exist, a new file will be created. Otherwise, the function will
 * overwrite the file with the source file.
 */
void cp(int count, char *src, char *dest)
{
	if (count > 3) {
		fprintf(stderr, "too many arguments\n");
		return;
	} else if (count < 3) {
		fprintf(stderr, "too few arguments\n");
		return;
	}
	int sfd, dfd, numread, numwritten;
	char buffer[BUF_SIZE];
	struct stat sfstat;
	sfd = open(src, O_RDONLY);
	if (sfd == -1) {
		perror("open");
		return;
	}
	if (lstat(src, &sfstat) == -1) {
		perror("lstat");
		return;
	}
	if ((sfstat.st_mode & S_IFMT) != S_IFREG) {
		fprintf(stderr, "source is not regular file\n");
		return;
	}
	umask(0);
	dfd = open(dest, O_WRONLY | O_CREAT | O_TRUNC, sfstat.st_mode);
	if (dfd == -1) {
		perror("open");
		return;
	}
	while (1) {
		numread = read(sfd, buffer, BUF_SIZE);
		if (numread == -1) {
			perror("read");
			return;
		}
		if (numread == 0)
			break;
		char *bufferptr = buffer;
		while (numread > 0) {
			numwritten = write(dfd, bufferptr, numread);
			if (numwritten == -1) {
				perror("write");
				return;
			}
			numread -= numwritten;
			bufferptr += numwritten;
		}
	}
	if (close(sfd) == -1) {
		perror("close");
		return;
	}
	if (close(dfd) == -1) {
		perror("close");
		return;
	}
}

/*
 * This function removes a file or a directory. Assumption is made that
 * there is no directory in the directory that the user wishes to remove.
 * When removing a directory, all files in it will be unlinked and then
 * only the directory can be removed.
 */
void rm(int count, char *filedir)
{
	if (count > 2) {
		fprintf(stderr, "too many arguments\n");
		return;
	} else if (count < 2) {
		fprintf(stderr, "too few arguments\n");
		return;
	}
	int fd, numread;
	struct stat fstat;
	fd = open(filedir, O_RDONLY);
	if (fd == -1) {
		perror("open");
		return;
	}
	if (lstat(filedir, &fstat) == -1) {
		perror("lstat");
		return;
	}
	if ((fstat.st_mode & S_IFMT) != S_IFDIR) {
		if (unlink(filedir) == -1) {
			perror("unlink");
			return;
		}
	} else {
		char buffer[BUF_SIZE];
		struct ldirent *de;
		numread = syscall(SYS_getdents, fd, buffer, BUF_SIZE);
		if (numread == -1) {
			perror("getdents");
			return;
		}
		int bufferptr;
		for (bufferptr = 0; bufferptr < numread;
		     bufferptr += de->reclen) {
			de = (struct ldirent *) (buffer + bufferptr);
			char *dename = (char *) de->name;
			if (strcmp(dename, ".") == 0
			    || strcmp(dename, "..") == 0)
				continue;
			/* String manipulation for forming fullpath */
			char fullpath[MAX_CHAR];
			strcpy(fullpath, filedir);
			if (fullpath[strlen(fullpath) - 1] != '/')
				strcat(fullpath, "/");
			strcat(fullpath, dename);
			if (unlink(fullpath) == -1) {
				perror("unlink");
				return;
			}
		}
		if (rmdir(filedir) == -1) {
			perror("rmdir");
			return;
		}
	}
	if (close(fd) == -1) {
		perror("close");
		return;
	}
}

/*
 * This is the entry point of the program. It provides a shell for
 * user to input commands. The shell will print output or error message
 * after the user has entered a command. The shell can only accept
 * maximum 1024 characters including the new line character for each line.
 * The program terminates only if there is any command-line arguments
 * given or the program reads EOF.
 */
int main(int argc, char *argv[])
{
	if (argc >= 2) {
		fprintf(stderr, "shell takes no arguments\n");
		exit(1);
	}

	int count;
	char line[MAX_CHAR];
	char command[MAX_CHAR];
	char arg1[MAX_CHAR];
	char arg2[MAX_CHAR];

	while (1) {
		if (feof(stdin))
			exit(0);
		fgets(line, MAX_CHAR, stdin);
		if (strlen(line) == MAX_CHAR - 1
		    && line[MAX_CHAR - 2] != '\n') {
			if (!feof(stdin)) {
				char extra[MAX_CHAR];
				/* Escape the remaining line */
				fgets(extra, MAX_CHAR, stdin);
				if (!
				    ((strlen(extra) == 1
				      && extra[0] == '\n')
				     || (strlen(extra) == 0))) {
					fprintf(stderr,
						"too long input\n");
					continue;
				}
			}
		}
		if (line[strlen(line) - 1] == '\n')
			line[strlen(line) - 1] = '\0';
		parseline(line, &count, command, arg1, arg2);
		if (*line == EOF)
			break;
		else if (!strcmp(command, "ls"))
			ls(count, arg1);
		else if (!strcmp(command, "cp"))
			cp(count, arg1, arg2);
		else if (!strcmp(command, "rm"))
			rm(count, arg1);
		else if (!strcmp(command, ""))
			continue;
		else
			fprintf(stderr, "unrecognised command %s\n", line);
		clear(&count, command, arg1, arg2);
	}

	return 0;
}
