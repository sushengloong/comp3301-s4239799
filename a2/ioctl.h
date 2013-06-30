#ifndef CRYPTO_IOCTL_H
#define CRYPTO_IOCTL_H

/* ioctl.h for COMP3301 A2.
 * Written by Sam Kingston, September 2010.
 * Version 1.2. See spec for changes (or diff on course website).
 */

enum crypto_direction {
    CRYPTO_READ,
    CRYPTO_WRITE
};

enum crypto_encmode {
    CRYPTO_ENC,
    CRYPTO_DEC,
    CRYPTO_PASSTHROUGH
};

/* Passed as a pointer to ioctl(CRYPTO_IOCSMODE) */
struct crypto_smode {
    enum crypto_direction dir;
    enum crypto_encmode mode;
    unsigned char key;
};

/* This doesn't exist in the kernel headers on the VM, only in user-space.
 * To be consistent with the spec, we just define it to be the same as
 * EOPNOTSUPP (which is what Linux does anyway). Make sure you use ENOTSUP
 * as the specification declares!
 */
#define ENOTSUP EOPNOTSUPP

#include <linux/ioctl.h>

/* Magic number for ioctls - this should be unique to the running kernel */
#define CRYPTO_MAGIC 0x3301

/* Create a buffer and return its identifier. This call takes no arguments.
 *
 * Example (where fd is an open file descriptor to the device):
 *
 *     int buffer_id = ioctl(fd, CRYPTO_IOCCREATE);
 *
 * Returns one of:
 *     > 0 on success (buffer id)
 *     -ENOMEM if no memory was available to satisfy the request
 */
#define CRYPTO_IOCCREATE _IO(CRYPTO_MAGIC, 1)

/* Deletes a buffer identified by the integer argument given.
 *
 * Example (where fd is an open file descriptor to the device): 
 *
 *     int buffer_id = 2;
 *     int r = ioctl(fd, CRYPTO_IOCTDELETE, buffer_id);
 *
 * Returns one of:
 *     0 on success
 *     -EINVAL if the buffer specified does not exist
 *     -ENOTSUP if the buffer has a positive reference count
 *         (except if the requesting fd is the only attached fd, which
 *         should succeed)
 *     -ENOMEM if no memory was available to satisfy the request
 */
#define CRYPTO_IOCTDELETE _IOW(CRYPTO_MAGIC, 2, int)

/* Attaches the file descriptor to a buffer identified by the integer argument
 * given. The fd must not be attached to any buffer at the time.
 *
 * Example (where fd is an open file descriptor to the device):
 *
 *     int buffer_id = 2;
 *     int r = ioctl(fd, CRYPTO_IOCTATTACH, buffer_id);
 *
 * Returns one of:
 *     0 on success
 *     -EINVAL if the buffer specified does not exist
 *     -ENOTSUP if the fd is already attached to a buffer or there is already
 *              a reader or writer attached (depending on requested mode)
 *     -ENOMEM if no memory was available to satisfy the request
 */
#define CRYPTO_IOCTATTACH _IOW(CRYPTO_MAGIC, 3, int)

/* Detach from the already attached buffer. Since the driver knows which
 * buffer a file descriptor is attached to, this call takes no argument.
 *
 * Example (where fd is an open file descriptor to the device):
 *
 *     int r = ioctl(fd, CRYPTO_IOCDETACH);
 *
 * Returns one of:
 *     0 on success
 *     -ENOTSUP if the fd is not attached to any buffer
 *     -ENOMEM if no memory was available to satisfy the request
 */
#define CRYPTO_IOCDETACH _IO(CRYPTO_MAGIC, 4)

/* Sets the mode of standard I/O calls, given by the struct passed as an
 * argument. You must initialise this struct first. The fd does not have
 * to be attached to a buffer for this call to work, since the encryption
 * mode is a property of the file descriptor, not a buffer.
 *
 * This can be called multiple times to set different modes (for instance
 * to set the write mode, and then the read mode).
 *
 * Example to set the device to decrypt on read (where fd is an open file 
 * descriptor to the device):
 *
 *     struct crypto_smode m;
 *     m.dir = CRYPTO_READ;
 *     m.mode = CRYPTO_DEC;
 *     m.key = 0x19;
 *
 *     int r = ioctl(fd, CRYPTO_IOCSMODE, &m);
 *
 * The structure will be copied from the userspace process' address space
 * by the device driver.
 *
 * Returns one of:
 *     0 on success
 *     -ENOMEM if no memory was available to satisfy the request
 *     -EFAULT if the pointer given is outside the address space of the
 *              user process
 */
#define CRYPTO_IOCSMODE _IOW(CRYPTO_MAGIC, 5, struct crypto_smode *)

#endif
