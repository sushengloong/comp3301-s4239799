/*
 *  linux/fs/ext2/file.c
 *
 * Copyright (C) 1992, 1993, 1994, 1995
 * Remy Card (card@masi.ibp.fr)
 * Laboratoire MASI - Institut Blaise Pascal
 * Universite Pierre et Marie Curie (Paris VI)
 *
 *  from
 *
 *  linux/fs/minix/file.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  ext2 fs regular file handling primitives
 *
 *  64-bit file support on 64-bit platforms by Jakub Jelinek
 * 	(jj@sunsite.ms.mff.cuni.cz)
 */

#include <linux/time.h>
#include <linux/aio.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <asm/uaccess.h>
#include <linux/pagemap.h>
#include <linux/buffer_head.h>
#include "ext2.h"
#include "xattr.h"
#include "acl.h"

/* Encryption key, only remains in memory */
int key = 0x0;

/*
 * Called when filp is released. This happens when all file descriptors
 * for a single struct file are closed. Note that different open() calls
 * for the same file yield different struct file structures.
 */
static int ext2_release_file(struct inode *inode, struct file *filp)
{
	if (filp->f_mode & FMODE_WRITE) {
		mutex_lock(&EXT2_I(inode)->truncate_mutex);
		ext2_discard_reservation(inode);
		mutex_unlock(&EXT2_I(inode)->truncate_mutex);
	}
	return 0;
}

/*
 * Called when writing to immediate file. If the file grows more than
 * 60 bytes, the immediate file will be converted to regular file.
 */
ssize_t ext3301_im_write(struct file *filp, const char __user *buf,
			 size_t len, loff_t *ppos)
{
	struct inode *inode = filp->f_dentry->d_inode;
	struct ext2_inode_info *ei = EXT2_I(inode);
	unsigned char *data = (unsigned char *) ei->i_data;
	ssize_t ret = 0;
	char *temp;
	struct page *page = NULL;
	struct ext2_dir_entry_2 *de;

	if (len + *ppos <= 60) {
		memcpy(data, buf, len);
		ret = len;
	} else {
		if (strlen(data) && (filp->f_flags & O_APPEND)) {
			temp =
			    kzalloc(sizeof(char) * (strlen(data) + len),
				    GFP_KERNEL);
			if (!temp)
				return -ENOMEM;
			memcpy(temp, data, strlen(data) - 1);
			strncat(temp, buf, len);
			memset(data, 0, strlen(data));
			ret =
			    do_sync_write(filp, temp, strlen(temp) + 1,
					  ppos);
			kfree(temp);
		} else {
			memset(data, 0, strlen(data));
			ret = do_sync_write(filp, buf, len, ppos);
		}
		inode->i_mode = S_IFREG | (inode->i_mode ^ S_IFIM);
		de = ext2_find_entry(filp->f_dentry->d_parent->d_inode,
			&filp->f_dentry->d_name, &page);
		if (!de)
			return -ENOENT;
		de->file_type = 1;
		kunmap(page);
		page_cache_release(page);
	}
	return ret;
}

/*
 * Called when reading immediate file.
 */
ssize_t ext3301_im_read(struct file *filp, char __user *buf, size_t len,
			loff_t *ppos)
{
	struct inode *inode = filp->f_dentry->d_inode;
	struct ext2_inode_info *ei = EXT2_I(inode);
	ssize_t ret = 0;
	unsigned char *data = (unsigned char *) ei->i_data;
	if (*ppos >= strlen(data))
		goto out;
	if (*ppos + len >= strlen(data)) {
		len = strlen(data) - *ppos;
		if (copy_to_user(buf, data, len))
			return -EFAULT;
	}
	*ppos += len;
	ret = len;
out:
	return ret;
}

/*
 * Replacement for the generic do_sync_read function.
 * It handles immediate and regular files.
 */
ssize_t ext3301_sync_read(struct file *filp, char __user *buf,
			  size_t len, loff_t *ppos)
{
	int i;
	ssize_t ret;
	char *temp;
	struct dentry *dir = filp->f_dentry->d_parent;
	struct inode *inode = filp->f_dentry->d_inode;
	mm_segment_t oldfs = get_fs();
	ext2_debug("Full Path: %s%s\n", dir->d_parent->d_name.name,
		   dir->d_name.name);
	if (strcmp(dir->d_parent->d_name.name, "/") == 0
	    && strcmp(dir->d_name.name, "encrypt") == 0) {
		temp = kzalloc(sizeof(char) * len, GFP_KERNEL);
		if (temp == NULL)
			return -ENOMEM;
		set_fs(KERNEL_DS);
		if (S_ISIM(inode->i_mode))
			ret = ext3301_im_read(filp, temp, len, ppos);
		else
			ret = do_sync_read(filp, temp, len, ppos);
		set_fs(oldfs);
		ext2_debug("Encrypted text: %s\n", temp);
		ext2_debug("Key: %x\n", key);
		for (i = 0; i < len; i++)
			if (temp[i] != '\0')
				temp[i] ^= key;
		if (copy_to_user(buf, temp, len)) {
			kfree(temp);
			return -EFAULT;
		}
		ext2_debug("Decrypted text: %s\n", temp);
		kfree(temp);
	} else {
		if (S_ISIM(inode->i_mode))
			ret = ext3301_im_read(filp, buf, len, ppos);
		else
			ret = do_sync_read(filp, buf, len, ppos);
	}
	return ret;
}

/*
 * Replacement for the generic do_sync_write function.
 * Called when writing to immediate or regular file.
 */
ssize_t ext3301_sync_write(struct file *filp, const char __user *buf,
			   size_t len, loff_t *ppos)
{
	int i;
	ssize_t ret;
	char *temp;
	struct dentry *dir = filp->f_dentry->d_parent;
	struct inode *inode = filp->f_dentry->d_inode;
	mm_segment_t oldfs = get_fs();
	ext2_debug("Full Path: %s%s\n", dir->d_parent->d_name.name,
		   dir->d_name.name);
	if (strcmp(dir->d_parent->d_name.name, "/") == 0
	    && strcmp(dir->d_name.name, "encrypt") == 0) {
		temp = kzalloc(sizeof(char) * len, GFP_KERNEL);
		if (temp == NULL)
			return -ENOMEM;
		if (copy_from_user(temp, buf, len)) {
			kfree(temp);
			return -EFAULT;
		}
		ext2_debug("Plaintext: %s\n", temp);
		ext2_debug("Key: %x\n", key);
		for (i = 0; i < len; i++) {
			ext2_debug("Before %d: %c\n", i, temp[i]);
			if (temp[i] != '\0')
				temp[i] ^= key;
			ext2_debug("After %d: %c\n", i, temp[i]);
		}
		ext2_debug("Ciphertext: %s\n", temp);
		set_fs(KERNEL_DS);
		if (S_ISIM(inode->i_mode))
			ret = ext3301_im_write(filp, temp, len, ppos);
		else
			ret = do_sync_write(filp, temp, len, ppos);
		set_fs(oldfs);
		ext2_debug("write ret: %d\n", ret);
		kfree(temp);
	} else {
		set_fs(KERNEL_DS);
		if (S_ISIM(inode->i_mode))
			ret = ext3301_im_write(filp, buf, len, ppos);
		else
			ret = do_sync_write(filp, buf, len, ppos);
	}
	ppos += len;
	ext2_debug("ext3301_sync_write returns\n");
	return ret;
}



/*
 * We have mostly NULL's here: the current defaults are ok for
 * the ext2 filesystem.
 */
const struct file_operations ext2_file_operations = {
	.llseek = generic_file_llseek,
	.read = ext3301_sync_read,
	.write = ext3301_sync_write,
	.aio_read = generic_file_aio_read,
	.aio_write = generic_file_aio_write,
	.unlocked_ioctl = ext2_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = ext2_compat_ioctl,
#endif
	.mmap = generic_file_mmap,
	.open = generic_file_open,
	.release = ext2_release_file,
	.fsync = simple_fsync,
	.splice_read = generic_file_splice_read,
	.splice_write = generic_file_splice_write,
};

#ifdef CONFIG_EXT2_FS_XIP
const struct file_operations ext2_xip_file_operations = {
	.llseek = generic_file_llseek,
	.read = xip_file_read,
	.write = xip_file_write,
	.unlocked_ioctl = ext2_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = ext2_compat_ioctl,
#endif
	.mmap = xip_file_mmap,
	.open = generic_file_open,
	.release = ext2_release_file,
	.fsync = simple_fsync,
};
#endif

const struct inode_operations ext2_file_inode_operations = {
	.truncate = ext2_truncate,
#ifdef CONFIG_EXT2_FS_XATTR
	.setxattr = generic_setxattr,
	.getxattr = generic_getxattr,
	.listxattr = ext2_listxattr,
	.removexattr = generic_removexattr,
#endif
	.setattr = ext2_setattr,
	.check_acl = ext2_check_acl,
	.fiemap = ext2_fiemap,
};
