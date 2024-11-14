#include <linux/kprobes.h>
#include <linux/printk.h>
#include <linux/file.h>
#include <linux/poll.h>
#include <linux/jiffies.h>
#include "header.h"

/* define cross-file variables */
struct proc_dir_entry *proc_entry, *proc_parent_entry;
struct ring_buffer *rbuf_read, *rbuf_poll;
struct kprobe **kp;

/* for poll */
DEFINE_SPINLOCK(lock);
DECLARE_WAIT_QUEUE_HEAD(wait_queue);
bool polled = false;

ssize_t proc_read(struct file *file, char __user *buffer, size_t count, loff_t *pos) {
    ssize_t ret;

    if (*pos > 0)
        return 0;

    if (polled) {
        /* simply get last event from global variable event */
        printk(KERN_INFO "DEBUG: polling\n");
        if (copy_to_user(buffer, monitor_entry, count < ENTRY_SIZE ? count : ENTRY_SIZE)) {
            ret = -EFAULT;
            goto exit;
        }

        ret = (ssize_t)ENTRY_SIZE;
        *pos = (loff_t)ENTRY_SIZE;
        polled = false;
    } else {
        char *out_buffer = kmalloc(BUFFER_SIZE, GFP_KERNEL);
        printk(KERN_INFO "DEBUG: reading\n");
        if (!out_buffer) {
            ret = -ENOMEM;
            kfree(out_buffer);
            goto exit;
        }

        ring_buffer_read(rbuf_read, out_buffer);
        if (copy_to_user(buffer, out_buffer, count < rbuf_read->size ? count : rbuf_read->size)) {
            kfree(out_buffer);
            ret = -EFAULT;
            goto exit;
        }

        ret = (ssize_t)rbuf_read->size;
        *pos = (loff_t)rbuf_read->size;
    }

exit:
    return ret;
}

static __poll_t proc_poll(struct file *file, poll_table *wait) {
    poll_wait(file, &wait_queue, wait);
    if (data_available) {
        polled = true;
        data_available = 0;
        return POLLIN | POLLRDNORM;
    }
    return 0;
}

const struct proc_ops proc_fops = {
        .proc_read = proc_read,
        .proc_poll = proc_poll,
};

static int __init my_kprobe_init(void) {
    int ret, i;

    rbuf_read = kmalloc(sizeof(struct ring_buffer), GFP_KERNEL);
    rbuf_poll = kmalloc(sizeof(struct ring_buffer), GFP_KERNEL);
    if (!rbuf_read || !rbuf_poll) {
        ring_buffer_destroy_both();
        return -ENOMEM;
    }
    ring_buffer_init_both();

    proc_parent_entry = proc_mkdir("fs_monitor", NULL);
    if (!proc_parent_entry) {
        ring_buffer_destroy_both();
        return -ENOMEM;
    }
    proc_entry = proc_create("extended_journal", 0444, proc_parent_entry, &proc_fops);
    if (!proc_entry) {
        proc_remove(proc_parent_entry);
        ring_buffer_destroy_both();
        return -ENOMEM;
    }

    /* alloc kprobes */
    kp = kmalloc(KPROBES_COUNT * sizeof(struct kprobe *), GFP_KERNEL);
    for (i = 0; i < KPROBES_COUNT; i++) {
        kp[i] = kmalloc(sizeof(struct kprobe), GFP_KERNEL);
        if (!kp[i]) {
            free_ptr_array((void **)kp, i);
            proc_remove(proc_entry);
            proc_remove(proc_parent_entry);
            ring_buffer_destroy_both();
            return -ENOMEM;
        }
    }

    /* NOTICE: all 'struct kprobe' must be fulfiled with, at least, symbol_name and pre(post)_handler */
    kp[0]->symbol_name = "vfs_write";
    kp[0]->pre_handler = vfs_write_trace;
    kp[1]->symbol_name = "do_unlinkat";
    kp[1]->pre_handler = do_unlinkat_trace;

    ret = register_kprobes(kp, KPROBES_COUNT);
    if (ret < 0) {
        printk(KERN_INFO "Failed to register kprobe: %d\n", ret);
        free_ptr_array((void **)kp, KPROBES_COUNT);
        proc_remove(proc_entry);
        proc_remove(proc_parent_entry);
        ring_buffer_destroy_both();
        return ret;
    }
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 4, 0)
    printk(KERN_WARNING "fs_monitor: resolving absolute paths directly from 'struct filename' "
                        "is not supported for kernels older than 6.4.0, so on deletion if "
                        "it was relative, you won't see real path\n");
#endif

    return 0;
}

static void __exit my_kprobe_exit(void) {
    unregister_kprobes(kp, KPROBES_COUNT);
    proc_remove(proc_entry);
    proc_remove(proc_parent_entry);
    free_ptr_array((void **)kp, KPROBES_COUNT);
    ring_buffer_destroy_both();
}

module_init(my_kprobe_init)
module_exit(my_kprobe_exit)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("max.orel.site@yandex.kz");
MODULE_DESCRIPTION("Monitor modifications in filesystems");
