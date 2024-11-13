#include <linux/kprobes.h>
#include <linux/printk.h>
#include <linux/file.h>
#include <linux/base64.h>
#include "header.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("max.orel.site@yandex.kz");
MODULE_DESCRIPTION("Kprobe example to track 'write' syscall");

static struct kprobe kp;
struct ring_buffer rbuf;
static struct proc_dir_entry *proc_entry;

static struct file_system_type *proc_fs, *sysfs_fs, *devtmpfs_fs, *tmpfs_fs, *ramfs_fs;

static int init_filesystem_pointers(void) {
    proc_fs = get_fs_type("proc");
    sysfs_fs = get_fs_type("sysfs");
    devtmpfs_fs = get_fs_type("devtmpfs");
    tmpfs_fs = get_fs_type("tmpfs");
    ramfs_fs = get_fs_type("ramfs");

    if (!proc_fs || !sysfs_fs || !devtmpfs_fs || !tmpfs_fs || !ramfs_fs) {
        pr_err("Error initializing filesystem pointers\n");
        return -ENODEV;
    }

    return 0;
}

static int is_service_fs(struct file *file) {
    struct super_block *sb = file->f_path.dentry->d_sb;

    if (sb->s_type == proc_fs ||
        sb->s_type == sysfs_fs ||
        sb->s_type == devtmpfs_fs ||
        sb->s_type == tmpfs_fs ||
        sb->s_type == ramfs_fs)
        return 1;

    return 0;
}

static int copy_middle(char *to, const char *from, size_t count) {
    if (count == 0)
        return 0;

    size_t write_count = count > COPY_BUF_SIZE ? COPY_BUF_SIZE : count;
    size_t start_pos = (count - write_count) / 2;
    if (copy_from_user(to, from + start_pos, write_count))
        return 0;

    return (int)write_count;
}

static int vfs_write_trace(struct kprobe *p, struct pt_regs *regs) {
    /* due to we handle 'vfs_write', not a 'write' syscall
     * we have data in normal registers (RDI, RSI, RDX)
     * not in strange reversed order (R10, R9, R8) */

    // taken from declaration of 'vfs_write' function
    struct file *file = (struct file *)regs->di;
    const char *buf = (const char *)regs->si;
    size_t count = (size_t)regs->dx;
    loff_t *ppos = (loff_t *)regs->cx;

    // some buffers
    char kbuf[COPY_BUF_SIZE],
         filename[MAX_PATH_LEN],
         base64_encoded[BASE64_ENCODED_MAX],
         entry[ENTRY_SIZE], *path;

    int write_count, r;
    size_t path_len;
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

    // build entry with '\0's as separators
    entry[0] = '\0';
    path = d_path(&file->f_path, filename, MAX_PATH_LEN);
    sprintf(entry + 1, "%s", path);
    path_len = strlen(path);
    entry[path_len + 1] = '\0';
    sprintf(entry + path_len + 2, "%s", base64_encoded);
    entry[path_len + r + 2] = '\0';
    entry[path_len + r + 3] = '\n';

    ring_buffer_append(&rbuf, entry, path_len + r + 4);
    return 0;
}


static int __init my_kprobe_init(void) {
    int ret;

    ret = init_filesystem_pointers();
    if (ret < 0)
        return ret;

    rbuf.data = kmalloc(BUFFER_SIZE, GFP_KERNEL);
    if (!rbuf.data)
        return -ENOMEM;
    ring_buffer_init(&rbuf);

    proc_entry = proc_create("fs_notifier", 0444, NULL, &proc_fops);

    kp.symbol_name = "vfs_write";
    kp.pre_handler = vfs_write_trace;

    ret = register_kprobe(&kp);
    if (ret < 0) {
        printk(KERN_INFO "Failed to register kprobe: %d\n", ret);
        proc_remove(proc_entry);
        kfree(rbuf.data);
        return ret;
    }

    return 0;
}

static void __exit my_kprobe_exit(void) {
    unregister_kprobe(&kp);
    proc_remove(proc_entry);
    kfree(rbuf.data);
}

module_init(my_kprobe_init)
module_exit(my_kprobe_exit)
