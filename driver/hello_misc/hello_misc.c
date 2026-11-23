#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/wait.h>
#include <linux/poll.h>

#define HELLO_IOC_MAGIC   'h'
#define HELLO_IOC_RESET   _IO(HELLO_IOC_MAGIC, 0)
#define HELLO_IOC_GETLEN  _IOR(HELLO_IOC_MAGIC, 1, int)

static char *hello_buf;
static int hello_len;
static wait_queue_head_t hello_wait;

static int hello_open(struct inode *inode, struct file *file)
{
    pr_info("hello: open\n");
    return 0;
}

static int hello_release(struct inode *inode, struct file *file)
{
    pr_info("hello: release\n");
    return 0;
}
						  
static ssize_t hello_read(struct file *file, char __user *ubuf,
						size_t count, loff_t *ppos)
{
  int n;

  if (*ppos >= hello_len) {
	  if (file->f_flags & O_NONBLOCK)
		  return -EAGAIN;
	  
		/* 等待队列唤醒 */
	  if (wait_event_interruptible(hello_wait, hello_len > *ppos))
		  return -ERESTARTSYS;
  }

  n = min((size_t)(hello_len - *ppos), count);
  if (copy_to_user(ubuf, hello_buf + *ppos, n))
	  return -EFAULT;

  *ppos += n;
  return n;
}						  

static ssize_t hello_write(struct file *file, const char __user *ubuf,
                           size_t count, loff_t *ppos)
{
    if (count > 255)
        count = 255;

    if (copy_from_user(hello_buf, ubuf, count))
        return -EFAULT;

    hello_buf[count] = '\0';
    hello_len = count;
	
	/* 重置读位置 */
    *ppos = 0;

	/* 唤醒等待队列 */
    wake_up_interruptible(&hello_wait);
    return count;
}						   

static long hello_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    int len = hello_len;

    switch (cmd) {
    case HELLO_IOC_RESET:
        hello_len = 0;
        memset(hello_buf, 0, 256);
        return 0;

    case HELLO_IOC_GETLEN:
        if (copy_to_user((int __user *)arg, &len, sizeof(len)))
            return -EFAULT;
        return 0;

    default:
        return -ENOTTY;
    }
}

/* 
*	用户态 poll() 阻塞等待，驱动有数据时唤醒它 
*/
static unsigned int hello_poll(struct file *file,
                               struct poll_table_struct *wait)
{
    unsigned int mask = 0;
	
	/* 当前进程挂到等待队列 */
    poll_wait(file, &hello_wait, wait);

    if (hello_len > file->f_pos)
        mask |= POLLIN | POLLRDNORM;

    return mask;
}

static const struct file_operations hello_fops = {
    .owner          = THIS_MODULE,
    .open           = hello_open,
    .release        = hello_release,
    .read           = hello_read,
    .write          = hello_write,
    .unlocked_ioctl = hello_ioctl,
    .poll           = hello_poll,	
};

static struct miscdevice hello_misc = {
    .minor = MISC_DYNAMIC_MINOR,
    .name  = "hello_misc",
    .fops  = &hello_fops,
};

static int __init hello_init(void)
{
    int ret;

    hello_buf = kzalloc(256, GFP_KERNEL);
    if (!hello_buf)
        return -ENOMEM;
	
	/* 初始化等待队列 */
	init_waitqueue_head(&hello_wait);
	
    ret = misc_register(&hello_misc);
    if (ret) {
        kfree(hello_buf);
        return ret;
    }

    pr_info("hello: /dev/%s registered\n", hello_misc.name);
    return 0;
}

static void __exit hello_exit(void)
{
    misc_deregister(&hello_misc);
    kfree(hello_buf);
    pr_info("hello: unloaded\n");
}

module_init(hello_init);
module_exit(hello_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("LinuxCamera hello misc driver");

