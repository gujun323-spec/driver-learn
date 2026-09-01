#include <linux/module.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/poll.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("A simple Hello World module");
MODULE_ALIAS("hello");

#define GLOBALMEM_SIZE 0x1000
#define HELLO_MAJOR 240
static int hello_major;
static struct class *hello_class;
static dev_t hello_devno;
static struct device *hello_device;

// 非阻塞读写的实现,测试
struct hello_dev {
	struct cdev cdev;
	unsigned char mem[GLOBALMEM_SIZE];
    struct mutex mutex;
    wait_queue_head_t r_wait; // 1. 定义一个用于读的等待队列头
    int has_data;             // 标志位：0代表没数据，1代表有数据
};

static struct hello_dev *hello_devp = NULL;

static int hello_open(struct inode *inode, struct file *filp)
{
    printk (KERN_INFO "hello_open mutex version!\n");
	filp->private_data = hello_devp;
	return 0;
}

static int hello_release(struct inode *inode, struct file *filp)
{
	return 0;
}

static ssize_t hello_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
	printk(KERN_INFO "hello_read mutex version!\n");
	struct hello_dev *dev = filp->private_data;
	if (mutex_lock_interruptible(&hello_devp->mutex))
		return -ERESTARTSYS;

	if (*f_pos >= sizeof(dev->mem)) {
		mutex_unlock(&hello_devp->mutex);
		return 0;
	}
	if (count > sizeof(dev->mem) - *f_pos)
		count = sizeof(dev->mem) - *f_pos;
	if (copy_to_user(buf, dev->mem + *f_pos, count)) {
		mutex_unlock(&hello_devp->mutex);
		return -EFAULT;
	}
	*f_pos += count;
	if (*f_pos >= sizeof(dev->mem))
		dev->has_data = 0;
	mutex_unlock(&hello_devp->mutex);
	return count;
}

// 锁的初始化问题
static ssize_t hello_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
	
	struct hello_dev *dev = filp->private_data;
	if (mutex_lock_interruptible(&hello_devp->mutex))
		return -ERESTARTSYS;
	if (*f_pos >= sizeof(dev->mem)) {
		mutex_unlock(&hello_devp->mutex);
		return -ENOSPC;
	}
	if (count > sizeof(dev->mem) - *f_pos)
		count = sizeof(dev->mem) - *f_pos;
	if (copy_from_user(dev->mem + *f_pos, buf, count)) {
		mutex_unlock(&hello_devp->mutex);
		return -EFAULT;
	}
	*f_pos += count;
	dev->has_data = 1;
	mutex_unlock(&hello_devp->mutex);
	printk(KERN_INFO "hello_write: data written, waking up readers\n");
	wake_up_interruptible(&dev->r_wait);
	return count;
}

static unsigned int hello_poll(struct file *filp, struct poll_table_struct *wait)
{
	struct hello_dev *dev = filp->private_data;
	unsigned int mask = 0;

	poll_wait(filp, &dev->r_wait, wait);

	if (mutex_lock_interruptible(&dev->mutex))
		return -ERESTARTSYS;
	if (dev->has_data)
		mask |= POLLIN | POLLRDNORM;
	if (dev->has_data == 0)
		mask |= POLLOUT | POLLWRNORM;
	mutex_unlock(&dev->mutex);

	return mask;
}

static const struct file_operations hello_fops = {
	.owner = THIS_MODULE,
	.open = hello_open,
	.release = hello_release,
	.read = hello_read,
	.write = hello_write,
	.poll = hello_poll,
};
static int __init hello_init(void)
{
	hello_devno = MKDEV(HELLO_MAJOR, 0);
    // 0是起点，1 是数量
	if (alloc_chrdev_region(&hello_devno, 0, 1, "hello") < 0) {
		printk(KERN_WARNING "hello: can't get major\n");
		return -1;
	}
	hello_major = MAJOR(hello_devno);
	printk(KERN_INFO "hello: major %d\n", hello_major);

	hello_devp = kmalloc(sizeof(struct hello_dev), GFP_KERNEL);
	if (!hello_devp) {
		unregister_chrdev_region(hello_devno, 1);
        printk(KERN_WARNING "hello: can't allocate memory for device\n");
		return -ENOMEM;
	}
	memset(hello_devp, 0, sizeof(*hello_devp));
	hello_devp->cdev.owner = THIS_MODULE;
	cdev_init(&hello_devp->cdev, &hello_fops);
	if (cdev_add(&hello_devp->cdev, hello_devno, 1) < 0) {
		kfree(hello_devp);
		unregister_chrdev_region(hello_devno, 1);
		return -EINVAL;
	}

    hello_class = class_create("hello");
    if (IS_ERR(hello_class)) {
        cdev_del(&hello_devp->cdev);
        unregister_chrdev_region(hello_devno, 1);
        return PTR_ERR(hello_class);
    }
    hello_device = device_create(hello_class, NULL, hello_devno, NULL, "hello");
    if (IS_ERR(hello_device)) {
        class_destroy(hello_class);
        cdev_del(&hello_devp->cdev);
        unregister_chrdev_region(hello_devno, 1);
        return PTR_ERR(hello_device);
    }
   
	printk(KERN_INFO "hello: device created successfully\n");
	mutex_init(&hello_devp->mutex);
	init_waitqueue_head(&hello_devp->r_wait);
	printk(KERN_INFO "Hello, World!\n");
	return 0;
}

static void __exit hello_exit(void)
{
     device_destroy(hello_class, hello_devno);
    class_destroy(hello_class);
	cdev_del(&hello_devp->cdev);
	kfree(hello_devp);
	unregister_chrdev_region(hello_devno, 1);
	printk(KERN_INFO "Goodbye, World!\n");
}

module_init(hello_init);
module_exit(hello_exit);
