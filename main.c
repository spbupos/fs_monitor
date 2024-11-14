#include <linux/kprobes.h>
#include <linux/printk.h>
#include <linux/file.h>
#include "header.h"

/* define cross-file variables */
struct proc_dir_entry *proc_entry, *proc_parent_entry;
struct ring_buffer *rbuf;
struct kprobe **kp;

ssize_t proc_read(struct file *file, char __user *buffer, size_t count, loff_t *pos) {
    char *out_buffer = kmalloc(BUFFER_SIZE + 1, GFP_KERNEL);
    if (*pos > 0)
        return 0;

    ring_buffer_read(rbuf, out_buffer);
    if (copy_to_user(buffer, out_buffer, rbuf->size))
        return -EFAULT;

    *pos = (loff_t)rbuf->size;
    kfree(out_buffer);
    return (ssize_t)rbuf->size;
}

const struct proc_ops proc_fops = {
        .proc_read = proc_read,
};

static int __init my_kprobe_init(void) {
    int ret, i;

    ret = init_filesystem_pointers();
    if (ret < 0)
        return ret;

    rbuf = kmalloc(sizeof(struct ring_buffer), GFP_KERNEL);
    ring_buffer_init(rbuf);

    proc_parent_entry = proc_mkdir("fs_monitor", NULL);
    if (!proc_parent_entry) {
        kfree(rbuf->data);
        return -ENOMEM;
    }
    proc_entry = proc_create("extended_journal", 0444, proc_parent_entry, &proc_fops);
    if (!proc_entry) {
        proc_remove(proc_parent_entry);
        ring_buffer_destroy(rbuf);
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
            ring_buffer_destroy(rbuf);
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
        ring_buffer_destroy(rbuf);
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
    ring_buffer_destroy(rbuf);
}

module_init(my_kprobe_init)
module_exit(my_kprobe_exit)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("max.orel.site@yandex.kz");
MODULE_DESCRIPTION("Monitor modifications in filesystems");
