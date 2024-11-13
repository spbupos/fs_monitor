#include <linux/kprobes.h>
#include <linux/printk.h>
#include <linux/file.h>
#include "header.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("max.orel.site@yandex.kz");
MODULE_DESCRIPTION("Kprobe example to track 'write' syscall");

static struct kprobe kp;
struct ring_buffer rbuf;
static struct proc_dir_entry *proc_entry;

static int copy_middle(char *to, const char *from, size_t count) {
    if (count == 0)
        return 0;

    size_t write_count = count > COPY_BUF_SIZE ? COPY_BUF_SIZE : count;
    size_t start_pos = (count - write_count) / 2;
    if (copy_from_user(to, from + start_pos, write_count))
        return 0;

    return (int)write_count;
}

static int handler_pre(struct kprobe *p, struct pt_regs *regs) {
    /* due to we handle 'vfs_write', not a 'write' syscall
     * we have data in normal registers (RDI, RSI, RDX)
     * not in strange reversed order (R10, R9, R8) */

    // taken from declaration of 'vfs_write' function
    struct file *file = (struct file *)regs->di;
    const char *buf = (const char *)regs->si;
    size_t count = (size_t)regs->dx;
    loff_t *ppos = (loff_t *)regs->cx;

    // buf to copy from userspace
    char kbuf[COPY_BUF_SIZE], filename[PATH_MAX], *path, entry[ENTRY_SIZE];
    int write_count;
    loff_t file_size, pos = *ppos;

    // we want work only with writes on real files
    if (!(file && S_ISREG(file->f_inode->i_mode)))
        return 0;

    // skip if we have append
    file_size = file->f_inode->i_size;
    if (file_size != 0 && pos == file_size)
        return 0;

    // extract filename from 'struct file'
    path = d_path(&file->f_path, filename, PATH_MAX);
    if (strstr(path, "/proc") ||
        strstr(path, "/sys") ||
        strstr(path, "/dev") ||
        strstr(path, "/tmp") ||
        strstr(path, "/run"))
        return 0;

    if ((write_count = copy_middle(kbuf, buf, count))) {
        sprintf(entry, "Written file: %s, data: %*ph\n", path, write_count, kbuf);
        ring_buffer_append(&rbuf, entry, strlen(entry));
    }

    return 0;
}


static int __init my_kprobe_init(void) {
    int ret;

    rbuf.data = kmalloc(BUFFER_SIZE, GFP_KERNEL);
    if (!rbuf.data)
        return -ENOMEM;
    ring_buffer_init(&rbuf);

    proc_entry = proc_create("my_proc", 0444, NULL, &proc_fops);

    kp.symbol_name = "vfs_write";
    kp.pre_handler = handler_pre;

    ret = register_kprobe(&kp);
    if (ret < 0) {
        printk(KERN_INFO "Failed to register kprobe: %d\n", ret);
        proc_remove(proc_entry);
        kfree(rbuf.data);
        return ret;
    }

    printk(KERN_INFO "Kprobe registered for write syscall\n");
    return 0;
}

static void __exit my_kprobe_exit(void) {
    unregister_kprobe(&kp);
    proc_remove(proc_entry);
    kfree(rbuf.data);
    printk(KERN_INFO "Kprobe unregistered\n");
}

module_init(my_kprobe_init)
module_exit(my_kprobe_exit)
