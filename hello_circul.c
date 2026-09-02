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
    unsigned int head; //写指针
    unsigned int tail; //读指针 

    wait_queue_head_t r_wait; //读等待队列
    wait_queue_head_t w_wait; //写等待队列
    struct mutex lock;     /* mutual exclusion semaphore*/
    spinlock_t spin_lock;       //切换到自旋锁，避免死锁
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
    unsigned int available, offset, size1, size2;
    // 判断是否有数据可读，如果没有则阻塞等待
    if(wait_event_interruptible(dev->r_wait, dev->head != dev->tail)) // 等待读队列，直到有数据可读
    {
        printk(KERN_INFO "hello_circul read: interrupted by signal\n");
        return -ERESTARTSYS;
    }
    //mutex_lock_interruptible(&dev->lock);
    spin_lock(&dev->spin_lock);
    //计算可读数据量
    available = dev->head  - dev->tail;
    if (count > available)
        count = available;  

    // 读取数据时可能会发生环绕，因此需要分两次读取
    offset = dev->tail & (HELLO_CIRCUL_SIZE - 1);    
    //前半部分的大小
    size1 = min(count, HELLO_CIRCUL_SIZE - offset);
    //后半部分的大小
    size2 = count - size1;

    if (copy_to_user(buf, dev->buffer + offset, size1)) {
        spin_unlock(&dev->spin_lock);
        return -EFAULT;
    }
    //第二次读取，是从头开始读取
    if (size2 && copy_to_user(buf + size1, dev->buffer, size2)) {
        spin_unlock(&dev->spin_lock);
        return -EFAULT;
    }
    dev->tail += count; //更新读指针
    spin_unlock(&dev->spin_lock);
    wake_up_interruptible(&dev->w_wait); //唤醒写等待队列    
    printk(KERN_INFO "hello_circul read\n");
    return count;
}
static ssize_t circul_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
    struct hello_circul *dev = filp->private_data;
    unsigned int available, offset, size1, size2;
    // 判断是否有空间可写，如果没有则阻塞等待
    if(wait_event_interruptible(dev->w_wait, (dev->head - dev->tail) != HELLO_CIRCUL_SIZE)) // 等待写队列，直到有空间可写
    {
        printk(KERN_INFO "hello_circul write: interrupted by signal\n");
        return -ERESTARTSYS;
    }
    spin_lock(&dev->spin_lock);
    //计算可写空间量
    available = HELLO_CIRCUL_SIZE - (dev->head - dev->tail);

    if (count > available)
        count = available;     
        
    // 2. 计算当前写指针在物理数组中的起始下标
    offset = dev->head & (HELLO_CIRCUL_SIZE - 1);  
    // 3. 计算从当前下标到物理数组末尾的“直线空闲空间”
    size1 = min((unsigned int)count, HELLO_CIRCUL_SIZE - offset);
    
    size2 = count - size1; // 4. 计算剩余的空间量（如果有的话）

    if (copy_from_user(dev->buffer + offset, buf, size1)) {
        spin_unlock(&dev->spin_lock);
        return -EFAULT; 
    }
    //第二次写入，是从头开始写入
    if (size2 && copy_from_user(dev->buffer, buf + size1, size2)) {
        spin_unlock(&dev->spin_lock);
        return -EFAULT;
    }
    dev->head += count; //更新写指针
    spin_unlock(&dev->spin_lock);
    wake_up_interruptible(&dev->r_wait); //唤醒读等待队列    
    printk(KERN_INFO "hello_circul write\n");
    return count;
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
    ret = alloc_chrdev_region(&circul_devno, 0, 1, "hello_circul");
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
    hello_circul_devp->head = 0;
    hello_circul_devp->tail = 0;
    circul_class = class_create("circul");
    if (IS_ERR(circul_class)) {
        cdev_del(&hello_circul_devp->cdev);
        unregister_chrdev_region(circul_devno, 1);
        return PTR_ERR(circul_class);
    }
    circul_device = device_create(circul_class, NULL, circul_devno, NULL, "circul");
    if (IS_ERR(circul_device)) {
        class_destroy(circul_class);
        cdev_del(&hello_circul_devp->cdev);
        unregister_chrdev_region(circul_devno, 1);
        return PTR_ERR(circul_device);
    }

    init_waitqueue_head(&hello_circul_devp->r_wait);
    init_waitqueue_head(&hello_circul_devp->w_wait);
    spin_lock_init(&hello_circul_devp->spin_lock);

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




