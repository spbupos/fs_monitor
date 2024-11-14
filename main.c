#include <linux/kprobes.h>
#include <linux/printk.h>
#include <linux/file.h>
#include <linux/poll.h>
#include <linux/device.h>
#include "header.h"

/* define cross-file variables */
struct ring_buffer *rbuf;
struct kprobe **kp;

/* for poll */
//DEFINE_SPINLOCK(lock);
DECLARE_WAIT_QUEUE_HEAD(wait_queue);
bool polled = false;

/* for chardev */
static struct class* tracer_class = NULL;
static struct device* tracer_device = NULL;
static int major;

ssize_t chardev_read(struct file *file, char __user *buffer, size_t count, loff_t *pos) {
    ssize_t ret;

    if (*pos > 0)
        return 0;

    if (polled) {
        /* simply get last event from global variable event */
        if (copy_to_user(buffer, monitor_entry, count < ENTRY_SIZE ? count : ENTRY_SIZE)) {
            ret = -EFAULT;
            goto exit;
        }

        ret = (ssize_t)ENTRY_SIZE;
        *pos = 0; // drop position because polling always starts from the beginning
        polled = false;
    } else {
        char *out_buffer = kmalloc(BUFFER_SIZE, GFP_KERNEL);
        printk(KERN_INFO "DEBUG: reading\n");
        if (!out_buffer) {
            ret = -ENOMEM;
            kfree(out_buffer);
            goto exit;
        }

        ring_buffer_read(rbuf, out_buffer);
        if (copy_to_user(buffer, out_buffer, count < rbuf->size ? count : rbuf->size)) {
            kfree(out_buffer);
            ret = -EFAULT;
            goto exit;
        }

        ret = (ssize_t)rbuf->size;
        *pos = (loff_t)rbuf->size;
    }

exit:
    return ret;
}

static __poll_t chardev_poll(struct file *file, poll_table *wait) {
    poll_wait(file, &wait_queue, wait);
    if (data_available) {
        printk(KERN_INFO "DEBUG: poll...\n");
        polled = true;
        data_available = false;
        return POLLIN | POLLRDNORM;
    }
    return 0;
}

const struct file_operations chardev_fops = {
        .read = chardev_read,
        .poll = chardev_poll,
};

static char *tracer_devnode(const struct device *dev, umode_t *mode) {
    if (mode)
        *mode = DEVMODE;
    return NULL;
}

static int __init my_kprobe_init(void) {
    int ret, i;

    rbuf = kmalloc(sizeof(struct ring_buffer), GFP_KERNEL);
    if (!rbuf) {
        return -ENOMEM;
    }
    ring_buffer_init(rbuf);

    major = register_chrdev(0, DEVNAME, &chardev_fops);
    if (major < 0) {
        ring_buffer_destroy(rbuf);
        return -ENOMEM;
    }

    tracer_class = class_create(CLASS_NAME);
    if (IS_ERR(tracer_class)) {
        unregister_chrdev(major, DEVNAME);
        ring_buffer_destroy(rbuf);
        pr_err("Failed to register device class\n");
        return PTR_ERR(tracer_class);
    }
    tracer_class->devnode = tracer_devnode;

    tracer_device = device_create(tracer_class, NULL, MKDEV(major, 0), NULL, DEVNAME);
    if (IS_ERR(tracer_device)) {
        class_destroy(tracer_class);
        unregister_chrdev(major, DEVNAME);
        ring_buffer_destroy(rbuf);
        pr_err("Failed to create the device\n");
        return PTR_ERR(tracer_device);
    }
    printk(KERN_INFO "Monitor registered at /dev/%s with major number %d\n", DEVNAME, major);

    /* alloc kprobes */
    kp = kmalloc(KPROBES_COUNT * sizeof(struct kprobe *), GFP_KERNEL);
    for (i = 0; i < KPROBES_COUNT; i++) {
        kp[i] = kmalloc(sizeof(struct kprobe), GFP_KERNEL);
        if (!kp[i]) {
            free_ptr_array((void **)kp, i);
            ring_buffer_destroy(rbuf);
            device_destroy(tracer_class, MKDEV(major, 0));
            class_unregister(tracer_class);
            class_destroy(tracer_class);
            unregister_chrdev(major, DEVNAME);
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
        ring_buffer_destroy(rbuf);
        device_destroy(tracer_class, MKDEV(major, 0));
        class_unregister(tracer_class);
        class_destroy(tracer_class);
        unregister_chrdev(major, DEVNAME);
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
    free_ptr_array((void **)kp, KPROBES_COUNT);
    ring_buffer_destroy(rbuf);
    device_destroy(tracer_class, MKDEV(major, 0));
    class_unregister(tracer_class);
    class_destroy(tracer_class);
    unregister_chrdev(major, DEVNAME);
}

module_init(my_kprobe_init)
module_exit(my_kprobe_exit)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("max.orel.site@yandex.kz");
MODULE_DESCRIPTION("Monitor modifications in filesystems");
