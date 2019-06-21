/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/init.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/wait.h>
#include <linux/param.h>
#include <linux/time.h>
#include <linux/timer.h>

struct jitimer_context {
	wait_queue_head_t	wq;
	int			done;
	struct timer_list	t;
};

static struct jitimer_driver {
	unsigned long		delay;
	struct proc_dir_entry	*proc;
	const char		*const name;
	const unsigned int	retry_nr;
	const unsigned int	default_delay_ms;
	struct file_operations	fops[1];
} jitimer_driver = {
	.retry_nr		= 5,	/* 5 retry */
	.default_delay_ms	= 10,	/* 10ms */
	.name			= "jitimer",
};

static void timer(struct timer_list *t)
{
	struct jitimer_context *ctx = container_of(t, struct jitimer_context, t);
	printk(KERN_DEBUG "hello from timer handler\n");
	ctx->done = 1;
	wake_up_interruptible(&ctx->wq);
}

static int show(struct seq_file *m, void *v)
{
	struct jitimer_driver *drv = m->private;
	struct jitimer_context *ctx;
	int ret;

	ctx = kzalloc(sizeof(struct jitimer_context), GFP_KERNEL);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);
	printk(KERN_DEBUG "prepare timer\n");
	init_waitqueue_head(&ctx->wq);
	timer_setup(&ctx->t, timer, 0);
	ctx->t.expires = jiffies + drv->delay;
	add_timer(&ctx->t);
	if (wait_event_interruptible(ctx->wq, ctx->done)) {
		ret = -ERESTARTSYS;
		goto out;
	}
	printk(KERN_DEBUG "finish timer\n");
	ret = 0;
out:
	del_timer_sync(&ctx->t);
	kfree(ctx);
	return ret;
}

static ssize_t write(struct file *fp, const char __user *buf, size_t count, loff_t *pos)
{
	return count;
}

static int open(struct inode *ip, struct file *fp)
{
	struct jitimer_driver *drv = PDE_DATA(ip);
	return single_open(fp, show, drv);
}

static int __init init(void)
{
	struct jitimer_driver *drv = &jitimer_driver;
	struct file_operations *fops = drv->fops;
	struct proc_dir_entry *proc;
	char name[15]; /* strlen("driver/")+strlen(drv->name)+1 */
	int err;

	err = snprintf(name, sizeof(name), "driver/%s", drv->name);
	if (err < 0)
		return err;
	fops->owner	= THIS_MODULE;
	fops->read	= seq_read;
	fops->write	= write;
	fops->open	= open;
	fops->release	= single_release;
	proc = proc_create_data(name, S_IRUGO|S_IWUSR, NULL, fops, drv);
	if (IS_ERR(proc))
		return PTR_ERR(proc);
	drv->delay	= HZ*drv->default_delay_ms/MSEC_PER_SEC;
	drv->proc	= proc;
	return 0;
}
module_init(init);

static void __exit term(void)
{
	struct jitimer_driver *drv = &jitimer_driver;
	proc_remove(drv->proc);
}
module_exit(term);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kei Nohguchi <kei@nohguchi.com>");
MODULE_DESCRIPTION("Just In Timer module");
