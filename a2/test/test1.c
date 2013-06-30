#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include "../ioctl.h"

/*
 * This program opens the driver for both reading and writing, create one
 * buffer, and write plaintext to the buffer. It should then read back
 * the encrypted ciphertext.
 */
int main(int argc, char *argv[])
{
	int fd = open("/dev/crypto", O_RDWR);
	if (fd < 0) {
		perror("open");
		close(fd);
		return 1;
	}
	int buffer_id = ioctl(fd, CRYPTO_IOCCREATE);
	char msg[] =
	    "COMP3301 is teaching me a lot about operating systems";
	struct crypto_smode m;
	m.dir = CRYPTO_WRITE;
	m.mode = CRYPTO_ENC;
	m.key = 0x13;
	int r = ioctl(fd, CRYPTO_IOCSMODE, &m);
	if (r != 0) {
		perror("ioctl");
		close(fd);
		return 1;
	}
	int ret = write(fd, msg, strlen(msg));
	if (ret < 0) {
		perror("write");
		close(fd);
		return 1;
	}
	printf("Wrote \"%s\" (%d bytes)\n", msg, ret);
	char ciphertext[strlen(msg)];
	ret = read(fd, ciphertext, ret);
	if (ret < 0) {
		perror("read");
		close(fd);
		return 1;
	}
	printf("Read %d bytes of ciphertext\n", ret);
	ret = ioctl(fd, CRYPTO_IOCTDELETE, buffer_id);
	if (ret != 0) {
		perror("ioctl");
		close(fd);
		return 1;
	}
	close(fd);
	return 0;
}
