#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/kernel.h>

#define SCULL_DEVICE_NUM	4
#define SCULL_QUANTUM 		4096
#define SCULL_QSET 		1000
#define SCULL_CLASS_NAME	"scull"

static int scull_qset = SCULL_QSET;
static int scull_quantum = SCULL_QUANTUM;

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

static inline int scull_trim_internal(struct scull_qset *dptr)
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
		scull_trim_internal(dptr);
		next = dptr->next;
		kfree(dptr);
	}

	dev->data = NULL;
	dev->size = 0;

	return 0;
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

static struct file_operations scull_fops = {
	.open = scull_open,
	.release = scull_release,
};

static int __init scull_init(void)
{
	int ret = 0, index = 0;

	g_scull_dev = kmalloc(sizeof(struct scull_dev), GFP_KERNEL);
	if (g_scull_dev == NULL) {
		printk(KERN_ERR "kmalloc failed\n");
		return -ENOMEM;
	}

	g_scull_dev->scull_class = class_create(THIS_MODULE, SCULL_CLASS_NAME);
	if (IS_ERR(g_scull_dev->scull_class)) {
		printk(KERN_ERR "class_create failed\n");
		ret = PTR_ERR(g_scull_dev->scull_class);
		goto class_create_err;
	}

	ret = alloc_chrdev_region(&g_scull_dev->dev_no, 0,
					SCULL_DEVICE_NUM, SCULL_CLASS_NAME);
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

	for (index = 0; index < SCULL_DEVICE_NUM; index++) {
		char name[64] = {0};
		dev_t dev_no = g_scull_dev->dev_no + index;
		snprintf(name, 64, "scull%d", index);
		device_create(g_scull_dev->scull_class, NULL, dev_no, NULL, name);
	}

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
	device_destroy(g_scull_dev->scull_class, SCULL_DEVICE_NUM);
	cdev_del(&g_scull_dev->dev);
	unregister_chrdev_region(g_scull_dev->dev_no, SCULL_DEVICE_NUM);
	class_destroy(g_scull_dev->scull_class);
	kfree(g_scull_dev);
}
module_exit(scull_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("YinZhongkai -- YinZhongkai@yeah.net");
