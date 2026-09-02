#include <linux/module.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#define HELLO_CIRCUL_SIZE	1024
#define CIRCUL_MAJOR 230

struct hello_circul
{
    char buffer[HELLO_CIRCUL_SIZE];
    struct cdev cdev;	  /* Char device structure		*/
};

static dev_t circul_devno;
static struct class *circul_class;
static struct device *circul_device;


static struct hello_circul *hello_circul_devp;

static int circul_open(struct inode *inode, struct file *filp)
{
    filp->private_data = hello_circul_devp;
    printk(KERN_INFO "hello_circul open\n");
    return 0;
}
static int circul_release(struct inode *inode, struct file *filp)
{
    printk(KERN_INFO "hello_circul release\n");
    return 0;
}
static ssize_t circul_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
    struct hello_circul *dev = filp->private_data;
    printk(KERN_INFO "hello_circul read\n");
    return count;
}
static ssize_t circul_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
    struct hello_circul *dev = filp->private_data;
    printk(KERN_INFO "hello_circul write\n");
    return 0;
}
static unsigned int circul_poll(struct file *filp, struct poll_table_struct *wait)
{
    printk(KERN_INFO "hello_circul poll\n"); 
    return 0;
}

static const struct file_operations hello_fops = {
	.owner = THIS_MODULE,
	.open = circul_open,
	.release = circul_release,
	.read = circul_read,
	.write = circul_write,
	.poll = circul_poll,
};
static int __init hello_circul_init(void)
{
    
    //分配设备号
    int ret;
    circul_devno = MKDEV(CIRCUL_MAJOR, 0);
    ret = alloc_chrdev_region(&circul_devno, 0, 1, "timer");
    if (ret < 0) {
        printk(KERN_ERR "alloc_chrdev_region failed\n");
        return ret;
    }

    hello_circul_devp = kmalloc(sizeof(struct hello_circul), GFP_KERNEL);
    if (!hello_circul_devp) {
        printk(KERN_ERR "kmalloc failed\n");
        ret = -ENOMEM;
        //goto err_kmalloc;
    }
    
    /*里面有锁，所以不能这么初始化*/
    //memset(hello_circul_devp, 0, sizeof(*hello_circul_devp));
    hello_circul_devp->cdev.owner = THIS_MODULE;
    cdev_init(&hello_circul_devp->cdev, &hello_fops);
    ret = cdev_add(&hello_circul_devp->cdev, circul_devno, 1);
    if (ret) {
        printk(KERN_ERR "cdev_add failed\n");
       // goto err_cdev_add;
    }
    circul_class = class_create("timer");
    if (IS_ERR(circul_class)) {
        cdev_del(&hello_circul_devp->cdev);
        unregister_chrdev_region(circul_devno, 1);
        return PTR_ERR(circul_class);
    }
    circul_device = device_create(circul_class, NULL, circul_devno, NULL, "timer");
    if (IS_ERR(circul_device)) {
        class_destroy(circul_class);
        cdev_del(&hello_circul_devp->cdev);
        unregister_chrdev_region(circul_devno, 1);
        return PTR_ERR(circul_device);
    }
	return 0;
}

static void __exit hello_circul_exit(void)
{
    device_destroy(circul_class, circul_devno);
    cdev_del(&hello_circul_devp->cdev);
    kfree(hello_circul_devp);
    unregister_chrdev_region(circul_devno, 1);
    class_destroy(circul_class);
}

module_init(hello_circul_init);
module_exit(hello_circul_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("GuJun");
MODULE_DESCRIPTION("A simple Hello circula module");
MODULE_ALIAS("hello_circul");




