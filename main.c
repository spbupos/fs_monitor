#include <linux/kprobes.h>
#include <linux/kernel.h>
#include <linux/file.h>
#include <linux/poll.h>
#include <linux/device.h>
#include "header.h"

/* define cross-file variables */
struct ring_buffer *rbuf;
struct kprobe **kp;

/* for poll */
DECLARE_WAIT_QUEUE_HEAD(wait_queue);
bool polled = false;

/* for chardev */
static struct class* tracer_class = NULL;
static struct device* tracer_device = NULL;
static int major;

static int kpc = 0;

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
        if (!out_buffer) {
            ret = -ENOMEM;
            kfree(out_buffer);
            goto exit;
        }

        ring_buffer_rread(rbuf, out_buffer);
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

static unsigned int chardev_poll(struct file *file, poll_table *wait) {
    poll_wait(file, &wait_queue, wait);
    if (data_available) {
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

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 3, 0)
static char *tracer_devnode(struct device *dev, mode_t *mode) {
#elif LINUX_VERSION_CODE < KERNEL_VERSION(6, 4, 0)
static char *tracer_devnode(struct device *dev, umode_t *mode) {
#else
static char *tracer_devnode(const struct device *dev, umode_t *mode) {
#endif
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

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 4, 0)
    tracer_class = class_create(THIS_MODULE, CLASS_NAME);
#else
    tracer_class = class_create(CLASS_NAME);
#endif
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
    kp = kmalloc(KPROBES_MAX_COUNT * sizeof(struct kprobe *), GFP_KERNEL);
    for (i = 0; i < KPROBES_MAX_COUNT; i++) {
        kp[i] = kmalloc(sizeof(struct kprobe), GFP_KERNEL);
        if (!kp[i]) {
            free_ptr_array((void **)kp, i);
            ring_buffer_destroy(rbuf);
            device_destroy(tracer_class, MKDEV(major, 0));
            class_destroy(tracer_class);
            unregister_chrdev(major, DEVNAME);
            return -ENOMEM;
        }
        /* NOTICE: for some kernels between 4.9 and 5.10 we have
         * -EINVAL if we don't set 0 to entite memory of kprobe
         * because of some garbage in memory, it seems */
        memset(kp[i], 0, sizeof(struct kprobe));
    }

    kp[kpc]->symbol_name = "vfs_write";
    kp[kpc++]->pre_handler = vfs_write_trace;
    kp[kpc]->symbol_name = "vfs_unlink";
    kp[kpc++]->pre_handler = vfs_unlink_trace;
    kp[kpc]->symbol_name = "vfs_rename";
    kp[kpc++]->pre_handler = vfs_rename_trace;
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 5, 0)
    kp[kpc]->symbol_name = "do_sendfile";
#else
    kp[kpc]->symbol_name = "vfs_copy_file_range";
#endif
    kp[kpc++]->pre_handler = vfs_copy_trace;

    ret = register_kprobes(kp, kpc);
    if (ret < 0) {
        printk(KERN_INFO "Failed to register kprobe: %d\n", ret);
        free_ptr_array((void **)kp, KPROBES_MAX_COUNT);
        ring_buffer_destroy(rbuf);
        device_destroy(tracer_class, MKDEV(major, 0));
        class_destroy(tracer_class);
        unregister_chrdev(major, DEVNAME);
        return ret;
    }

    return 0;
}

static void __exit my_kprobe_exit(void) {
    unregister_kprobes(kp, kpc);
    free_ptr_array((void **)kp, KPROBES_MAX_COUNT);
    ring_buffer_destroy(rbuf);
    device_destroy(tracer_class, MKDEV(major, 0));
    class_destroy(tracer_class);
    unregister_chrdev(major, DEVNAME);
}

module_init(my_kprobe_init)
module_exit(my_kprobe_exit)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("max.orel.site@yandex.kz");
MODULE_DESCRIPTION("Monitor modifications in filesystems");
