#include <linux/module.h>
#include <linux/kprobes.h>
#include <linux/uaccess.h>
#include <linux/printk.h>
#include <linux/file.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("max.orel.site@yandex.kz");
MODULE_DESCRIPTION("Kprobe example to track 'write' syscall");

#define COPY_BUF_SIZE 40

static struct kprobe kp;

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
    size_t count = regs->dx;
    loff_t *pos = (loff_t *)regs->cx;

    // buf to copy from userspace
    char kbuf[COPY_BUF_SIZE];
    char filename[PATH_MAX];
    char *path;
    int write_count;
    loff_t file_size;

    // we want work only with writes on real files
    if (!(file && S_ISREG(file->f_inode->i_mode)))
        return 0;

    // check if we have append (pos == file_size)
    file_size = file->f_inode->i_size;
    if (file_size != 0 && *pos == file_size)
        return 0;

    // extract filename from 'struct file'
    path = d_path(&file->f_path, filename, PATH_MAX);
    if (strstr(path, "/proc") ||
        strstr(path, "/sys") ||
        strstr(path, "/dev") ||
        strstr(path, "/tmp") ||
        strstr(path, "/run"))
        return 0;

    if ((write_count = copy_middle(kbuf, buf, count)))
        printk(KERN_INFO "Written file: %s, data: %*ph\n", path, write_count, kbuf);

    return 0;
}


static int __init my_kprobe_init(void) {
    int ret;

    kp.symbol_name = "vfs_write";
    kp.pre_handler = handler_pre;

    ret = register_kprobe(&kp);
    if (ret < 0) {
        printk(KERN_INFO "Failed to register kprobe: %d\n", ret);
        return ret;
    }
    printk(KERN_INFO "Kprobe registered for write syscall\n");
    return 0;
}

static void __exit my_kprobe_exit(void) {
    unregister_kprobe(&kp);
    printk(KERN_INFO "Kprobe unregistered\n");
}

module_init(my_kprobe_init)
module_exit(my_kprobe_exit)
