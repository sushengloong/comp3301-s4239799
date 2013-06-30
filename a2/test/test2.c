#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include "../ioctl.h"

/*
 * This program forks immediately after starting, with both the parent and
 * child opening the device for reading and writing. The parent creates a
 * single buffer and pass the identifier to its child. The child then
 * attaches to the buffer.
 * The parent will then write plaintext data to the buffer which should be
 * encrypted by the device. The child will perform a decrypt on read on the
 * buffer and the device should yield the plaintext that was originally
 * written.
 */
int main(int argc, char *argv[])
{
	int pfd[2], fd = 0;
	pid_t cpid;
	int buf;
	int ret, r;
	if (pipe(pfd) == -1) {
		perror("pipe");
		exit(1);
	}
	cpid = fork();
	if (cpid == -1) {
		perror("fork");
		exit(1);
	}
	if (cpid == 0) {
		close(pfd[1]);
		ret = read(pfd[0], &buf, sizeof(int));
		if (ret < 0) {
			perror("id read");
			return 1;
		}
		int cfd = open("/dev/crypto", O_RDONLY);
		if (ioctl(cfd, CRYPTO_IOCTATTACH, buf)) {
			perror("ioctl");
			close(cfd);
			close(fd);
			return 1;
		}
		char dec_msg[8192];
		struct crypto_smode cm;
		cm.dir = CRYPTO_READ;
		cm.mode = CRYPTO_DEC;
		cm.key = 0x13;
		r = ioctl(cfd, CRYPTO_IOCSMODE, &cm);
		if (r != 0) {
			perror("ioctl");
			close(cfd);
			close(fd);
			return 1;
		}
		ret = read(cfd, dec_msg, 37);
		if (ret < 0) {
			perror("msg read");
			close(cfd);
			close(fd);
			return 1;
		}
		printf("\"%s\" (%d bytes)\n", dec_msg, ret);
		ret = ioctl(cfd, CRYPTO_IOCDETACH);
		if (ret != 0) {
			perror("ioctl");
			close(cfd);
			close(fd);
			return 1;
		}
		close(cfd);
		close(pfd[0]);
		exit(0);
	} else {
		close(pfd[0]);
		fd = open("/dev/crypto", O_WRONLY);
		if (fd < 0) {
			perror("open");
			return 1;
		}
		int buffer_id = ioctl(fd, CRYPTO_IOCCREATE);
		char msg[] = "A2 is quite an interesting assignment";
		struct crypto_smode m;
		m.dir = CRYPTO_WRITE;
		m.mode = CRYPTO_ENC;
		m.key = 0x13;
		r = ioctl(fd, CRYPTO_IOCSMODE, &m);
		if (r != 0) {
			perror("ioctl");
			return 1;
		}
		ret = write(fd, msg, strlen(msg));
		if (ret < 0) {
			perror("write");
			return 1;
		}
		printf("\"%s\" (%d bytes)\n", msg, ret);
		write(pfd[1], &buffer_id, sizeof(int));
		close(pfd[1]);
		wait(NULL);
		close(fd);
		exit(0);
	}
}
