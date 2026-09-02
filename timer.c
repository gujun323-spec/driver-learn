#include <linux/module.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/timer.h>    // 必须引入内核定时器头文件
#include <linux/spinlock.h> // 必须引入自旋锁头文件

#define TIMER_MAJOR 230

struct hello_timer_priv
{
    char share_data;          /* 唯一的共享数据槽 */
    spinlock_t lock;          /* 🌟 必须使用自旋锁：因为定时器回调属于中断上下文，严禁休眠锁 Mutex */
    struct timer_list timer;  /* 1. 定义内核定时器结构体 */
    struct cdev cdev;
};

static dev_t timer_devno;
static struct class *timer_class;
static struct device *timer_device;
static struct hello_timer_priv *hello_timer_devp;

/* 
 * 2. 定时器中断回调函数
 *    当设定的时间戳到达时，内核会自动在【硬中断上下文】中调用此函数。
 */
static void my_timer_callback(struct timer_list *t)
{
    // 利用 from_timer 宏，通过成员 timer 的指针反向安全捞出驱动全局私有结构体
    struct hello_timer_priv *dev = from_timer(dev, t, timer);
    unsigned long flags;

    // A. 核心安全天条：中断上下文加锁必须使用 spin_lock_irqsave
    // 此时会关闭当前 CPU 的硬中断响应，并保存原本的中断寄存器状态到 flags 中
    spin_lock_irqsave(&dev->lock, flags);

    // B. 【临界区】执行纯内存原子操作，让共享槽位的数据循环递增
    dev->share_data++;
    
    // 我们在内核日志中打印当前的计数值（注意：硬中断上下文允许 printk，但耗时要极短）
    printk(KERN_INFO "hello_timer: Timer triggered! share_data updated to: %d\n", dev->share_data);

    // C. 临界区结束，立刻解锁并恢复中断状态
    spin_unlock_irqrestore(&dev->lock, flags);

    // D. 核心周期性动作：利用 mod_timer 自动刷新下一次触发的时间戳
    // jiffies 是内核全局时钟节拍计数器，msecs_to_jiffies(1000) 代表将 1000 毫秒换算为对应节拍数
    mod_timer(&dev->timer, jiffies + msecs_to_jiffies(1000)); 
}

/* 
 * 极简配置：因为用户不再读写，我们只需保留最基础的 open 和 release 骨架，
 * 甚至为了绝对的纯净，file_operations 也可以直接设为空，系统会自动处理文件开关。
 */
static int timer_open(struct inode *inode, struct file *filp)
{
    filp->private_data = hello_timer_devp;
    return 0;
}

static int timer_release(struct inode *inode, struct file *filp)
{
    return 0;
}

static const struct file_operations hello_fops = {
	.owner   = THIS_MODULE,
	.open    = timer_open,
	.release = timer_release,
};

/* 3. 驱动模块初始化入口 */
static int __init hello_timer_init(void)
{
    int ret;
    timer_devno = MKDEV(TIMER_MAJOR, 0);
    ret = alloc_chrdev_region(&timer_devno, 0, 1, "hello_timer");
    if (ret < 0) {
        printk(KERN_ERR "alloc_chrdev_region failed\n");
        return ret;
    }

    hello_timer_devp = kmalloc(sizeof(struct hello_timer_priv), GFP_KERNEL);
    if (!hello_timer_devp) {
        unregister_chrdev_region(timer_devno, 1);
        return -ENOMEM;
    }
    
    // A. 核心组件显式初始化（铁律：必须在使用前完成）
    spin_lock_init(&hello_timer_devp->lock);
    
    hello_timer_devp->share_data = 0;

    // B. 初始化内核定时器，并将它与我们的回调函数 my_timer_callback 进行强绑定
    timer_setup(&hello_timer_devp->timer, my_timer_callback, 0);

    // C. 字符设备标准注册
    hello_timer_devp->cdev.owner = THIS_MODULE;
    cdev_init(&hello_timer_devp->cdev, &hello_fops);
    ret = cdev_add(&hello_timer_devp->cdev, timer_devno, 1);
    if (ret) {
        kfree(hello_timer_devp);
        unregister_chrdev_region(timer_devno, 1);
        return ret;
    }

    timer_class = class_create("mytimer");
    if (IS_ERR(timer_class)) {
        cdev_del(&hello_timer_devp->cdev);
        kfree(hello_timer_devp);
        unregister_chrdev_region(timer_devno, 1);
        return PTR_ERR(timer_class);
    }
    timer_device = device_create(timer_class, NULL, timer_devno, NULL, "mytimer");
    if (IS_ERR(timer_device)) {
        class_destroy(timer_class);
        cdev_del(&hello_timer_devp->cdev);
        kfree(hello_timer_devp);
        unregister_chrdev_region(timer_devno, 1);
        return PTR_ERR(timer_device);
    }

    // D. 🌟 导火索激活：使用 mod_timer 启动定时器，设定在 1 秒（jiffies + HZ）后触发第一次中断
    mod_timer(&hello_timer_devp->timer, jiffies + HZ);

    printk(KERN_INFO "hello_timer: Module loaded. Global Timer started.\n");
	return 0;
}

/* 4. 驱动模块注销出口 */
static void __exit hello_timer_exit(void)
{
    // 🌟 天条铁律：注销模块前，必须无条件同步删除定时器！
    // del_timer_sync 会确保即便定时器回调正在其他 CPU 核心上运行，也会等待其彻底执行完再注销。
    // 如果漏掉这一句，模块卸载后内核指针变为野指针，下一次时钟中断爆发时系统会瞬间发生死机崩溃（Kernel Panic）。
    del_timer_sync(&hello_timer_devp->timer);

    device_destroy(timer_class, timer_devno);
    class_destroy(timer_class);
    cdev_del(&hello_timer_devp->cdev);
    kfree(hello_timer_devp);
    unregister_chrdev_region(timer_devno, 1);
    
    printk(KERN_INFO "hello_timer: Module unloaded. Global Timer safety stopped.\n");
}

module_init(hello_timer_init);
module_exit(hello_timer_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("GuJun");
MODULE_DESCRIPTION("Pure Kernel Timer Autonomous Module");
MODULE_ALIAS("hello_timer");
