#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/uaccess.h>

#define SCULL_QUANTUM 		4096
#define SCULL_QSET 		1000
#define SCULL_CLASS_NAME	"scull"
#define SCULL_DEVICE_NAME	"scull"
#define SCULL_DEVICE_NUM	1

struct scull_qset {
	void **data;
	struct scull_qset *next;
};

struct scull_dev {
	struct scull_qset *data;
	int quantum;
	int qset;
	unsigned long size;
	unsigned int access_key;
	dev_t dev_no;
	struct semaphore sem;
	struct cdev dev;
	struct class *scull_class;
};

static struct scull_dev *g_scull_dev;

static inline int scull_trim_internal(struct scull_qset *dptr, int scull_qset)
{
	int i;

	if (dptr == NULL)
		return -1;

	if (dptr->data == NULL)
		return 0;

	for (i = 0; i < scull_qset; i++)
		kfree(dptr->data[i]);
	kfree(dptr->data);
	dptr->data = NULL;

	return 0;
}

static int scull_trim(struct scull_dev *dev)
{
	struct scull_qset *dptr, *next;

	for (dptr = dev->data; dptr; dptr = next) {
		scull_trim_internal(dptr, dev->qset);
		next = dptr->next;
		kfree(dptr);
	}

	dev->data = NULL;
	dev->size = 0;

	return 0;
}

static struct scull_qset *scull_follow(struct scull_dev *dev, int item)
{
	struct scull_qset *dptr;

	if (dev == NULL || item < 0) {
		printk(KERN_ERR "invalid param\n");
		return NULL;
	}

	dptr = dev->data;
	if (dptr == NULL) {
		dptr = dev->data = kmalloc(sizeof(struct scull_qset),
								GFP_KERNEL);
		if (dptr == NULL) {
			printk(KERN_ERR "kmlloc failed\n");
			return NULL;
		}
		memset(dptr, 0, sizeof(struct scull_qset));
	}

	while(item--) {
		if (!dptr->next) {
			dptr->next = kmalloc(sizeof(struct scull_qset),
								GFP_KERNEL);
			if (!dptr->next) {
				printk(KERN_ERR "kmlloc failed\n");
				return NULL;
			}
			memset(dptr->next, 0, sizeof(struct scull_qset));
		}
		dptr = dptr->next;
	}

	return dptr;
}

static int scull_open(struct inode *inode, struct file *filp)
{
	struct scull_dev *dev;

	dev = container_of(inode->i_cdev, struct scull_dev, dev);
	filp->private_data = dev;

	if ((filp->f_flags & O_ACCMODE) == O_WRONLY) {
		scull_trim(dev);
	}

	return 0;
}

static int scull_release(struct inode *inode, struct file *filp)
{
	return 0;
}

static ssize_t scull_read(struct file *filp, char __user *buf,
					size_t count, loff_t *f_pos)
{
	struct scull_dev *dev = filp->private_data;
	struct scull_qset *dptr;
	int quantum = dev->quantum;
	int qset = dev->qset;
	int item_size = quantum * qset;

	int item, s_pos, q_pos, rest;
	ssize_t retval = 0;

	if (down_interruptible(&dev->sem))
		return -ERESTARTSYS;

	if (*f_pos >= dev->size)
		goto out;

	if (*f_pos + count > dev->size)
		count = dev->size - *f_pos;

	item = (long)*f_pos / item_size;
	rest = (long)*f_pos % item_size;
	s_pos = rest / quantum;
	q_pos = rest % quantum;

	dptr = scull_follow(dev, item);
	if (dptr == NULL || !dptr->data || !dptr->data[s_pos])
		goto out;

	if (count > quantum - q_pos)
		count = quantum - q_pos;

	if (copy_to_user(buf, dptr->data[s_pos] + q_pos, count)) {
		retval = -EFAULT;
		goto out;
	}
	*f_pos += count;
	retval = count;

out:
	up(&dev->sem);
	return retval;
}

static ssize_t scull_write(struct file *filp, const char __user *buf,
					size_t count, loff_t *f_pos)
{
	struct scull_dev *dev = filp->private_data;
	struct scull_qset *dptr;
	int quantum = dev->quantum;
	int qset = dev->qset;
	int item_size = quantum * qset;

	int item, s_pos, q_pos, rest;
	ssize_t retval = -ENOMEM;

	if (down_interruptible(&dev->sem))
		return -ERESTARTSYS;

	item = (long)*f_pos / item_size;
	rest = (long)*f_pos % item_size;
	s_pos = rest / quantum;
	q_pos = rest % quantum;

	dptr = scull_follow(dev, item);
	if (dptr == NULL)
		goto out;

	if (!dptr->data) {
		dptr->data = kmalloc(qset * sizeof(char *), GFP_KERNEL);
		if (!dptr->data)
			goto out;
		memset(dptr->data, 0, qset * (sizeof(char *)));
	}

	if (!dptr->data[s_pos]) {
		dptr->data[s_pos] = kmalloc(quantum, GFP_KERNEL);
		if (!dptr->data[s_pos])
			goto out;
		memset(dptr->data[s_pos], 0, quantum);
	}

	if (count > quantum - q_pos)
		count = quantum - q_pos;

	if (copy_from_user(dptr->data[s_pos] + q_pos, buf, count)) {
		retval = -EFAULT;
		goto out;
	}

	*f_pos += count;
	retval = count;

	if (dev->size < *f_pos)
		dev->size = *f_pos;

out:
	up(&dev->sem);
	return retval;
}

static struct file_operations scull_fops = {
	.owner = THIS_MODULE,
	.open = scull_open,
	.release = scull_release,
	.read = scull_read,
	.write = scull_write,
};

static int scull_init_cdev(struct scull_dev *dev)
{
	if (dev == NULL) {
		printk(KERN_ERR "invalid param\n");
		return -1;
	}

	dev->quantum = SCULL_QUANTUM;
	dev->qset = SCULL_QSET;
	sema_init(&dev->sem, 1);

	return 0;
}

static int __init scull_init(void)
{
	int ret = 0;

	g_scull_dev = kmalloc(sizeof(struct scull_dev), GFP_KERNEL);
	if (g_scull_dev == NULL) {
		printk(KERN_ERR "kmalloc failed\n");
		return -ENOMEM;
	}
	memset(g_scull_dev, 0, sizeof(struct scull_dev));

	scull_init_cdev(g_scull_dev);

	g_scull_dev->scull_class = class_create(THIS_MODULE, SCULL_CLASS_NAME);
	if (IS_ERR(g_scull_dev->scull_class)) {
		printk(KERN_ERR "class_create failed\n");
		ret = PTR_ERR(g_scull_dev->scull_class);
		goto class_create_err;
	}

	ret = alloc_chrdev_region(&g_scull_dev->dev_no, 0,
					SCULL_DEVICE_NUM, SCULL_DEVICE_NAME);
	if (ret != 0) {
		printk(KERN_ERR "alloc_chrdev_region failed\n");
		goto alloc_chrdev_region_err;
	}

	cdev_init(&g_scull_dev->dev, &scull_fops);

	ret = cdev_add(&g_scull_dev->dev, g_scull_dev->dev_no, SCULL_DEVICE_NUM);
	if (ret != 0) {
		printk(KERN_ERR "cdev_add failed\n");
		goto cdev_add_err;
	}

	device_create(g_scull_dev->scull_class, NULL,
			g_scull_dev->dev_no, NULL, SCULL_DEVICE_NAME);

	return 0;

cdev_add_err:
	unregister_chrdev_region(g_scull_dev->dev_no, SCULL_DEVICE_NUM);
alloc_chrdev_region_err:
	class_destroy(g_scull_dev->scull_class);
class_create_err:
	kfree(g_scull_dev);

	return ret;
}
module_init(scull_init);

static void __exit scull_exit(void)
{
	device_destroy(g_scull_dev->scull_class, g_scull_dev->dev_no);
	cdev_del(&g_scull_dev->dev);
	unregister_chrdev_region(g_scull_dev->dev_no, SCULL_DEVICE_NUM);
	class_destroy(g_scull_dev->scull_class);
	scull_trim(g_scull_dev);
	kfree(g_scull_dev);
}
module_exit(scull_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("YinZhongkai -- YinZhongkai@yeah.net");
