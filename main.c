#include <linux/kprobes.h>
#include <linux/printk.h>
#include <linux/file.h>
#include <linux/version.h>
#include <linux/namei.h>

#if LINUX_VERSION_CODE > KERNEL_VERSION(6, 0, 0)
#include <linux/base64.h>
#endif

#include "header.h"

/* in x86_64 registers is used for arguments passing: rdi, rsi, rdx, rcx
 * WARNING: in some cases can be transformed to r10, r9, r8, rdx
 */

static int vfs_write_trace(struct kprobe *p, struct pt_regs *regs) {
    /* taken from declaration of 'vfs_write' function
     * ssize_t vfs_write(struct file *file, const char __user *buf, size_t count, loff_t *pos)
     */
    struct file *file = (struct file *)regs->di;
    const char *buf = (const char *)regs->si;
    size_t count = (size_t)regs->dx;
    loff_t *ppos = (loff_t *)regs->cx;

    // some buffers
    char kbuf[COPY_BUF_SIZE],
         filename[MAX_PATH_LEN],
         base64_encoded[BASE64_ENCODED_MAX],
         entry[ENTRY_SIZE], *path;

    int write_count;
    size_t r;
    loff_t file_size, pos = *ppos;

    // we want work only with writes on real files
    if (!(file && S_ISREG(file->f_inode->i_mode)))
        return 0;

    // skip if we have append
    file_size = file->f_inode->i_size;
    if (file_size != 0 && pos == file_size)
        return 0;

    if (is_service_fs(file))
        return 0;

    write_count = copy_middle(kbuf, buf, count);
    r = base64_encode(kbuf, write_count, base64_encoded);
    base64_encoded[r] = '\0';

    // finally we can build entry
    path = d_path(&file->f_path, filename, MAX_PATH_LEN);
    r = entry_combiner(entry, path, strlen(path), base64_encoded, r);
    ring_buffer_append(&rbuf, entry, r);

    return 0;
}

static int do_unlinkat_trace(struct kprobe *p, struct pt_regs *regs) {
    /* taken from declaration of 'do_unlinkat' function
     * int do_unlinkat(int dfd, struct filename *name)
     */
    struct filename *name = (struct filename *)regs->si;
    char entry[ENTRY_SIZE], filename[MAX_PATH_LEN];
    char *parent_path, absolute_path[MAX_PATH_LEN];
    size_t r;

    struct path parent;
    struct qstr last;
    int type, ret;

    ret = vfs_path_parent_lookup(name, 0, &parent, &last, &type, NULL);
    if (ret < 0 || is_service_fs_dentry(parent.dentry))
        return 0;

    parent_path = d_path(&parent, filename, MAX_PATH_LEN);
    r = sprintf(absolute_path, "%s/%.*s", parent_path, last.len, last.name);

    r = entry_combiner(entry, absolute_path, r, "<deleted>", 9);
    ring_buffer_append(&rbuf, entry, r);

    return 0;
}

ssize_t proc_read(struct file *file, char __user *buffer, size_t count, loff_t *pos) {
    char *out_buffer = kmalloc(BUFFER_SIZE + 1, GFP_KERNEL);
    if (*pos > 0)
        return 0;

    ring_buffer_read(&rbuf, out_buffer);
    if (copy_to_user(buffer, out_buffer, rbuf.size))
        return -EFAULT;

    *pos = rbuf.size;
    kfree(out_buffer);
    return rbuf.size;
}

static int __init my_kprobe_init(void) {
    int ret, i;

    ret = init_filesystem_pointers();
    if (ret < 0)
        return ret;

    rbuf.data = kmalloc(BUFFER_SIZE, GFP_KERNEL);
    if (!rbuf.data)
        return -ENOMEM;
    ring_buffer_init(&rbuf);

    proc_parent_entry = proc_mkdir("fs_monitor", NULL);
    if (!proc_parent_entry) {
        kfree(rbuf.data);
        return -ENOMEM;
    }
    proc_entry = proc_create("extended_journal", 0444, proc_parent_entry, &proc_fops);
    if (!proc_entry) {
        proc_remove(proc_parent_entry);
        kfree(rbuf.data);
        return -ENOMEM;
    }

    // alloc kprobes
    for (i = 0; i < KPROBES_COUNT; i++) {
        kp[i] = kmalloc(sizeof(struct kprobe), GFP_KERNEL);
        if (!kp[i]) {
            for (int j = 0; j < i; j++)
                kfree(kp[j]);
            proc_remove(proc_entry);
            proc_remove(proc_parent_entry);
            kfree(rbuf.data);
            return -ENOMEM;
        }
    }

    // NOTICE: all 'struct kprobe' must be fulfiled with, at least, symbol_name and pre_handler
    kp[0]->symbol_name = "vfs_write";
    kp[0]->pre_handler = vfs_write_trace;
    kp[1]->symbol_name = "do_unlinkat";
    kp[1]->pre_handler = do_unlinkat_trace;

    ret = register_kprobes(kp, KPROBES_COUNT);
    if (ret < 0) {
        printk(KERN_INFO "Failed to register kprobe: %d\n", ret);
        proc_remove(proc_entry);
        kfree(rbuf.data);
        return ret;
    }

    return 0;
}

static void __exit my_kprobe_exit(void) {
    unregister_kprobes(kp, KPROBES_COUNT);
    proc_remove(proc_entry);
    proc_remove(proc_parent_entry);
    kfree(rbuf.data);
    for (int i = 0; i < KPROBES_COUNT; i++)
        kfree(kp[i]);
}

module_init(my_kprobe_init)
module_exit(my_kprobe_exit)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("max.orel.site@yandex.kz");
MODULE_DESCRIPTION("Monitor modifications in filesystems");
