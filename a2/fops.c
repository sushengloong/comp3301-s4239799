#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/cdev.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/vmalloc.h>
#include <asm/uaccess.h>
#include <asm/page.h>
#include <asm/io.h>

#include "ioctl.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("SU Sheng Loong & TEE Lip Jian");
MODULE_DESCRIPTION("COMP3301 Assignment 2");

#define DEV_NAME "crypto"
#define BUF_SIZE 8192

/*
 * struct for reusable detached id
 */
struct detached_id {
	struct list_head list;
	unsigned int id;
};

/*
 * struct for holding buffer and associating data
 */
struct dev_buf {
	int id;
	unsigned int refcount;
	unsigned int readoff;
	unsigned int writeoff;
	unsigned int count;
	char *buf;
	int read_ref;
	int write_ref;
	int enc_last;
	int dec_last;
};

/*
 * list of device buffer struct
 */
struct dev_buf_list {
	struct list_head list;
	struct dev_buf *buf;
};

/*
 * private data stored in the file descriptor
 */
struct priv_data {
	int buf_id;
	struct crypto_smode readsmode;
	struct crypto_smode writesmode;
};

/** function prototype */
static int freebuf(struct dev_buf_list *);
static void search_entry(int, struct dev_buf_list **);
static void substr(const char[], char[], int, int, int);
static void encrypt(struct dev_buf *, char[], int, int);
static void decrypt(struct dev_buf *, char[], int, int);

static int new_id = 1;
static unsigned int ndev = 1;
static dev_t dev;
static struct cdev *mycdev;

static struct list_head detached_id_head;
static struct list_head dev_buf_head;

/*
 * When the device is opened by a file descriptor,
 * a private data struct will be allocated for the file descriptor.
 * All necessary data and counters will be initilised.
 */
static int device_open(struct inode *inode, struct file *filp)
{
	struct priv_data *p_data;
	/* increase the refcount of the open module */
	try_module_get(THIS_MODULE);
	p_data = kzalloc(sizeof(struct priv_data), GFP_KERNEL);
	if (!p_data) {
		printk(KERN_WARNING
		       "%s: private data memory allocation failed! \n",
		       DEV_NAME);
		return -ENOMEM;
	}
	p_data->buf_id = 0;
	p_data->readsmode.dir = CRYPTO_READ;
	p_data->readsmode.mode = CRYPTO_PASSTHROUGH;
	p_data->readsmode.key = 0;
	p_data->writesmode.dir = CRYPTO_WRITE;
	p_data->writesmode.mode = CRYPTO_PASSTHROUGH;
	p_data->writesmode.key = 0;
	filp->private_data = p_data;
	printk(KERN_INFO "%s: device opened successfully\n", DEV_NAME);
	return 0;
}

/*
 * This function is called when the device is released by a file descriptor.
 * The private data of the file decriptor will be clean up.
 * Any associated buffers with the file descriptor will be destroyed as well
 */
static int device_release(struct inode *inode, struct file *filp)
{
	struct priv_data *private_data;
	struct dev_buf_list *entry = NULL;
	printk(KERN_INFO "%s: release device\n", DEV_NAME);
	/* decrease the refcount of the open module */
	module_put(THIS_MODULE);
	private_data = (struct priv_data *) filp->private_data;
	if (private_data != NULL && private_data->buf_id != 0) {
		search_entry(private_data->buf_id, &entry);
		if (entry == NULL) {
			printk(KERN_INFO "%s: failed to search buffer\n",
			       DEV_NAME);
			return -EINVAL;
		}
		entry->buf->refcount--;
		if (entry->buf->refcount == 0)
			if (freebuf(entry) == -ENOMEM)
				return -ENOMEM;
		if (private_data) {
			printk(KERN_INFO
			       "%s: free private_data's memory\n",
			       DEV_NAME);
			kfree(private_data);
		}
	}
	printk(KERN_INFO "%s: device closed successfully\n", DEV_NAME);
	return 0;
}

/*
 * This function is called when a process reads the device via the
 * file descriptor. If the fd is not attached to any buffer,
 * an error will be returned. The text read will depends on
 * the mode set in the private data of the fd.
 */
static ssize_t device_read(struct file *filp, char *buf, size_t len,
			   loff_t *off)
{
	struct dev_buf_list *entry;
	struct priv_data *private_data;
	struct dev_buf *this_buf;
	unsigned long left;
	size_t wraplen;
	int wrap;
	int err;
	ssize_t ret;
	int id;
	char *temp;
	printk(KERN_INFO "%s: start reading\n", DEV_NAME);
	left = 0;
	wraplen = 0;
	wrap = 0;
	err = 0;
	ret = 0;
	entry = NULL;
	private_data = (struct priv_data *) filp->private_data;

	if (private_data == NULL || private_data->buf_id == 0)
		return -ENOTSUP;
	id = private_data->buf_id;
	search_entry(id, &entry);
	if (entry == NULL)
		return -EINVAL;
	this_buf = entry->buf;
	if (len > this_buf->count) {
		len = this_buf->count;
		err = 1;
	}
	if (this_buf->readoff + len > BUF_SIZE) {
		wraplen = (len - BUF_SIZE) - this_buf->readoff;
		len = BUF_SIZE - this_buf->readoff;
		wrap = 1;
	}
	temp = vmalloc(sizeof(char) * (len + wraplen));
	if (!temp) {
		printk(KERN_WARNING
		       "%s: temp vmalloc memory allocation failed! \n",
		       DEV_NAME);
		return -ENOMEM;
	}
	if (wrap) {
		substr(this_buf->buf, temp, this_buf->readoff, 0, len);
		substr(this_buf->buf, temp, 0, len, wraplen);
	} else {
		substr(this_buf->buf, temp, this_buf->readoff, 0, len);
	}

	switch (private_data->readsmode.mode) {
	case CRYPTO_ENC:
		printk(KERN_INFO "%s: reading with encryption\n",
		       DEV_NAME);
		encrypt(this_buf, temp, len + wraplen,
			private_data->readsmode.key);
		break;
	case CRYPTO_DEC:
		printk(KERN_INFO "%s: reading with decryption\n",
		       DEV_NAME);
		decrypt(this_buf, temp, len + wraplen,
			private_data->readsmode.key);
		break;
	default:
		break;
	}
	if (copy_to_user(buf, temp, len)) {
		if (temp)
			vfree(temp);
		return -EFAULT;
	}
	if (temp)
		vfree(temp);
	this_buf->count -= (len + wraplen);
	this_buf->readoff += (len + wraplen);
	this_buf->readoff %= BUF_SIZE;
	ret = len + wraplen;
	if (err)
		return -ENOBUFS;
	return ret;
}

/*
 * This function is called when a process writes to the device
 * via a fd. If the fd is not attached to any buffer, an error
 * will be returned. Once again the text written to the buffer
 * in the kernel depends on the mode set in the private data
 * of the file descriptor.
 */
static ssize_t device_write(struct file *filp, const char *buf, size_t len,
			    loff_t *off)
{

	struct dev_buf_list *entry = NULL;

	size_t wraplen = 0;
	int wrap = 0, err = 0;
	ssize_t ret = 0;
	int id;
	struct priv_data *private_data =
	    (struct priv_data *) filp->private_data;
	struct dev_buf *this_buf;
	char *temp;
	printk(KERN_INFO "%s: start writing\n", DEV_NAME);
	if (private_data == NULL || private_data->buf_id == 0)
		return -ENOTSUP;
	id = private_data->buf_id;
	printk(KERN_INFO "id : %d\n", id);
	search_entry(id, &entry);
	if (entry == NULL)
		return -EINVAL;
	this_buf = entry->buf;
	if (this_buf->count > 0
	    && this_buf->writeoff + len >= this_buf->readoff + BUF_SIZE) {
		len = BUF_SIZE - this_buf->writeoff - this_buf->readoff;
		err = 1;
	}
	if (this_buf->writeoff + len > BUF_SIZE) {
		wraplen = this_buf->writeoff + len - BUF_SIZE;
		len = BUF_SIZE - this_buf->writeoff;
		wrap = 1;
	}

	temp = vmalloc(sizeof(char) * (len + wraplen));
	if (!temp) {
		printk(KERN_WARNING
		       "%s: temp vmalloc memory allocation failed! \n",
		       DEV_NAME);
		return -ENOMEM;
	}
	if (copy_from_user(temp, buf, wraplen + len)) {
		if (temp)
			vfree(temp);
		return -EFAULT;
	}

	switch (private_data->writesmode.mode) {
	case CRYPTO_ENC:
		printk(KERN_INFO "%s: writing with encryption\n",
		       DEV_NAME);
		encrypt(this_buf, temp, len + wraplen,
			private_data->writesmode.key);
		break;
	case CRYPTO_DEC:
		printk(KERN_INFO "%s: writing with decryption\n",
		       DEV_NAME);
		decrypt(this_buf, temp, len + wraplen,
			private_data->writesmode.key);
		break;
	default:
		break;
	}
	if (wrap) {
		printk(KERN_INFO "%s: writing with wrap-around\n",
		       DEV_NAME);
		substr(temp, this_buf->buf, 0, this_buf->writeoff, len);
		substr(temp, this_buf->buf, len, 0, wraplen);
	} else {
		substr(temp, this_buf->buf, 0, this_buf->writeoff, len);
	}
	vfree(temp);
	this_buf->count += len + wraplen;
	this_buf->writeoff += len + wraplen;
	this_buf->writeoff %= BUF_SIZE;
	ret = wraplen + len;
	printk(KERN_INFO "%s: data written", DEV_NAME);
	if (err)
		return -ENOBUFS;
	return ret;
}

/*
 * Operations other than read/write should be called by using this
 * function. The allowed operations have been defined in the ioctl.h
 * header file. The user space process should pass in the appropriate
 * command and parameter if any to call the correct operation.
 */
static int device_ioctl(struct inode *inode, struct file *filp,
			unsigned int cmd, unsigned long arg)
{
	struct dev_buf_list *entry = NULL;
	struct priv_data *private_data = NULL;
	struct dev_buf *new_dev_buf;
	struct dev_buf_list *node;
	struct crypto_smode *p;
	struct crypto_smode tempsmode;
	int id;

	switch (cmd) {
	case CRYPTO_IOCCREATE:
		private_data = (struct priv_data *) filp->private_data;
		if (private_data->buf_id != 0)
			return -ENOTSUP;
		new_dev_buf = kzalloc(sizeof(struct dev_buf), GFP_KERNEL);
		if (!new_dev_buf) {
			printk(KERN_WARNING
			       "%s: buffer memory allocation failed!\n",
			       DEV_NAME);
			return -ENOMEM;
		}
		new_dev_buf->buf = vmalloc(sizeof(char) * BUF_SIZE);
		if (!new_dev_buf->buf) {
			printk(KERN_WARNING
			       "%s: vmalloc buffer allocation failed!\n",
			       DEV_NAME);
			return -ENOMEM;
		}
		if (list_empty(&detached_id_head))
			new_dev_buf->id = new_id++;
		else {
			struct detached_id *ident =
			    list_entry(detached_id_head.next,
				       struct detached_id, list);
			new_dev_buf->id = ident->id;
			list_del(&ident->list);
			if (ident) {
				printk(KERN_INFO
				       "%s: free id node's memory\n",
				       DEV_NAME);
				kfree(ident);
			}
		}
		new_dev_buf->refcount = 1;
		new_dev_buf->readoff = 0;
		new_dev_buf->writeoff = 0;
		new_dev_buf->count = 0;
		new_dev_buf->enc_last = -1;
		new_dev_buf->dec_last = -1;
		if (filp->f_mode & FMODE_READ)
			new_dev_buf->read_ref = 1;
		if (filp->f_mode & FMODE_WRITE)
			new_dev_buf->write_ref = 1;
		private_data->buf_id = new_dev_buf->id;
		node = kzalloc(sizeof(struct dev_buf_list), GFP_KERNEL);
		if (!node) {
			printk(KERN_WARNING
			       "%s: node memory allocation failed!\n",
			       DEV_NAME);
			return -ENOMEM;
		}
		node->buf = new_dev_buf;
		INIT_LIST_HEAD(&node->list);
		list_add(&node->list, &dev_buf_head);
		printk(KERN_INFO "%s: new buffer with id %d created\n",
		       DEV_NAME, new_dev_buf->id);
		printk(KERN_INFO "%s: head info %d\n", DEV_NAME,
		       node->buf->id);
		return new_dev_buf->id;
	case CRYPTO_IOCTDELETE:
		id = (int) arg;
		private_data = (struct priv_data *) filp->private_data;
		search_entry(id, &entry);
		if (entry == NULL)
			return -EINVAL;
		if (entry->buf->refcount > 1)
			return -ENOTSUP;
		private_data->buf_id = 0;
		if (freebuf(entry) == -ENOMEM)
			return -ENOMEM;
		return 0;
	case CRYPTO_IOCTATTACH:
		private_data = (struct priv_data *) filp->private_data;
		if (private_data != NULL && private_data->buf_id != 0)
			return -ENOTSUP;
		id = (int) arg;
		search_entry(id, &entry);
		if (entry == NULL)
			return -EINVAL;
		if ((filp->f_mode & FMODE_READ) && entry->buf->read_ref)
			return -ENOTSUP;
		if ((filp->f_mode & FMODE_WRITE) && entry->buf->write_ref)
			return -ENOTSUP;
		if (filp->f_mode & FMODE_READ)
			entry->buf->read_ref = 1;
		if (filp->f_mode & FMODE_WRITE)
			entry->buf->write_ref = 1;
		entry->buf->refcount++;
		private_data->buf_id = entry->buf->id;
		return 0;
	case CRYPTO_IOCDETACH:
		private_data = (struct priv_data *) filp->private_data;
		if (private_data == NULL || private_data->buf_id == 0)
			return -ENOTSUP;
		search_entry(private_data->buf_id, &entry);
		if (entry == NULL)
			return -EINVAL;
		if (filp->f_mode & FMODE_READ)
			entry->buf->read_ref = 0;
		if (filp->f_mode & FMODE_WRITE)
			entry->buf->write_ref = 0;
		entry->buf->refcount--;
		private_data->buf_id = 0;
		if (entry->buf->refcount == 0)
			if (freebuf(entry) == -ENOMEM)
				return -ENOMEM;
		return 0;
	case CRYPTO_IOCSMODE:
		private_data = (struct priv_data *) filp->private_data;
		p = (struct crypto_smode *) arg;
		if (copy_from_user(&tempsmode, p,
				   sizeof(struct crypto_smode))) {
			printk(KERN_WARNING
			       "%s: error copying crypto_smode to kernel\n",
			       DEV_NAME);
			return -EFAULT;
		}
		if (tempsmode.dir == CRYPTO_WRITE)
			private_data->writesmode = tempsmode;
		else
			private_data->readsmode = tempsmode;
		return 0;
	default:
		return -EINVAL;
	}
	return 0;
}

/*
 * This function allows the kernel space buffer to be mapped to the
 * user space process which calls the function.
 */
static int device_mmap(struct file *filp, struct vm_area_struct *vma)
{
	unsigned long size;
	unsigned long offset;
	int ret;
	unsigned long start = vma->vm_start;
	unsigned long pfn;
	struct dev_buf_list *entry = NULL;
	char *ptr;
	struct priv_data *private_data =
	    (struct priv_data *) filp->private_data;
	if (private_data->buf_id == 0)
		return -ENOTSUP;
	size = vma->vm_end - vma->vm_start;
	if (size % PAGE_SIZE != 0 || size > BUF_SIZE)
		return -EIO;
	offset = vma->vm_pgoff;
	if (offset % PAGE_SIZE != 0 || offset > BUF_SIZE)
		return -EIO;
	search_entry(private_data->buf_id, &entry);
	if (entry == NULL) {
		printk(KERN_INFO "%s: Invalid buffer id in fd when mmap\n",
		       DEV_NAME);
		return -EINVAL;
	}
	ptr = entry->buf->buf;
	while (size > 0) {
		pfn = vmalloc_to_pfn(ptr);
		ret = remap_pfn_range(vma, start, pfn, PAGE_SIZE,
					   PAGE_SHARED);
		if (ret < 0)
			return ret;
		start += PAGE_SIZE;
		ptr += PAGE_SIZE;
		size -= PAGE_SIZE;
	}
	/*
	   if (remap_pfn_range
	   (vma, vma->vm_start,
	   virt_to_phys((void *)((unsigned long)entry->buf->buf))
	   >> PAGE_SHIFT,
	   size, vma->vm_page_prot))
	   return -EIO;
	 */
	return 0;
}

/*
 * This function search for a buffer in the kernel given the id.
 */
static void search_entry(int id, struct dev_buf_list **entry)
{
	struct list_head *ptr;
	list_for_each(ptr, &dev_buf_head) {
		*entry = list_entry(ptr, struct dev_buf_list, list);
		if ((*entry)->buf->id == id)
			return;
	}
	entry = NULL;
}

/*
 * This function frees buffer allocated for a particular buffer
 */
static int freebuf(struct dev_buf_list *entry)
{
	struct detached_id *idnode =
	    kzalloc(sizeof(struct detached_id), GFP_KERNEL);
	if (!idnode) {
		printk(KERN_WARNING
		       "%s: id node memory allocation failed\n", DEV_NAME);
		return -ENOMEM;
	}
	idnode->id = entry->buf->id;
	if (entry->buf) {
		printk(KERN_INFO "%s: free buffer's memory\n", DEV_NAME);
		if (entry->buf->buf)
			vfree(entry->buf->buf);
		kfree(entry->buf);
	}
	list_del(&entry->list);
	if (entry) {
		printk(KERN_INFO "%s: free buffer node's memory\n",
		       DEV_NAME);
		kfree(entry);
	}
	INIT_LIST_HEAD(&idnode->list);
	list_add(&idnode->list, &detached_id_head);
	return 0;
}

/*
 * This function assigns the substring of a particular string to
 * another string which are passed in as arguments.
 */
static void substr(const char str[], char sub[], int off, int suboff,
		   int len)
{
	int i;
	for (i = 0; i < len; i++, suboff++)
		sub[suboff] = str[off + i];
}

/*
 * This function encrypts the parameter string with the key specified.
 */
static void encrypt(struct dev_buf *dbuf, char plain[], int len, int key)
{
	int i;
	for (i = 0; i < len; i++) {
		if (i == 0) {
			if (dbuf->enc_last != -1)
				plain[i] =
				    plain[i] ^ dbuf->buf[dbuf->enc_last];
			else
				plain[i] = plain[i] ^ key;

		} else {
			plain[i] = plain[i] ^ plain[i - 1];
		}
	}
	dbuf->enc_last += len;
	dbuf->enc_last %= BUF_SIZE;
}

/*
 * This function decrypts the parameter string with the key specified.
 */
static void decrypt(struct dev_buf *dbuf, char cipher[], int len, int key)
{
	char previous;
	int i;
	for (i = 0; i < len; i++) {
		char now = cipher[i];
		if (i == 0) {
			if (dbuf->dec_last != -1)
				cipher[i] =
				    cipher[i] ^ dbuf->buf[dbuf->dec_last];
			else
				cipher[i] = cipher[i] ^ key;
		} else {
			cipher[i] = cipher[i] ^ previous;
		}
		previous = now;
	}
	dbuf->dec_last += len;
	dbuf->dec_last %= BUF_SIZE;
}

/*
 * Initialise the file operation struct
 */
struct file_operations fops = {
	.open = device_open,
	.release = device_release,
	.read = device_read,
	.write = device_write,
	.ioctl = device_ioctl,
	.mmap = device_mmap
};

/*
 * This function is called when the module is loaded to the kernel.
 * All necessary structs will be setup and the device numbers will
 * printed to the kernel ring buffer.
 */
int __init init_module(void)
{
	int ret = alloc_chrdev_region(&dev, 0, ndev, DEV_NAME);
	if (ret < 0) {
		printk(KERN_WARNING
		       "%s error %d: allocating char device region\n",
		       DEV_NAME, ret);
		return ret;
	}
	mycdev = cdev_alloc();
	mycdev->ops = &fops;
	mycdev->owner = THIS_MODULE;
	ret = cdev_add(mycdev, dev, 1);
	if (ret) {
		printk(KERN_WARNING "%s error %d: adding cdev\n",
		       DEV_NAME, ret);
		return ret;
	}
	INIT_LIST_HEAD(&detached_id_head);
	INIT_LIST_HEAD(&dev_buf_head);
	printk(KERN_INFO "%s: module with major %d, minor %d inserted\n",
	       DEV_NAME, MAJOR(dev), MINOR(dev));
	return 0;
}

/*
 * This function is called when the module is unloaded from the kernel.
 * The character device struct will be deleted and the device will be
 * unregistered.
 */
void __exit cleanup_module(void)
{
	cdev_del(mycdev);
	unregister_chrdev_region(dev, ndev);
	printk(KERN_INFO "%s: module removed\n", DEV_NAME);
}
