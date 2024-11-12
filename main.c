#include <linux/module.h>
#include <linux/kprobes.h>
#include <linux/uaccess.h>
#include <linux/printk.h>
#include <linux/file.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("max.orel.site@yandex.kz");
MODULE_DESCRIPTION("Kprobe example to track 'write' syscall");

#define COPY_BUF_SIZE 5

static struct kprobe kp;

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
    // buf for filename (max 255 bytes)
    char filename[PATH_MAX];
    char *path;
    // real bytes count to write
    int real_count = count < COPY_BUF_SIZE ? (int)count : COPY_BUF_SIZE;

    // we want work only with writes on real files
    if (!(file && S_ISREG(file->f_inode->i_mode)))
        return 0;

    // extract filename from 'struct file'
    path = d_path(&file->f_path, filename, PATH_MAX);
    if (strstr(path, "/proc") ||
        strstr(path, "/sys") ||
        strstr(path, "/dev") ||
        strstr(path, "/tmp") ||
        strstr(path, "/run"))
        return 0;

    // copy buffer from userspace
    if (copy_from_user(kbuf, buf, real_count) == 0)
        printk(KERN_INFO "Written file: %s, data: %*ph, count: %lu, pos: %llu\n", path, real_count, kbuf, count, *pos);

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
