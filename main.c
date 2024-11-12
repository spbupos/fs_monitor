#include <linux/module.h>
#include <linux/kprobes.h>
#include <linux/uaccess.h>
#include <linux/printk.h>
#include <linux/file.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("max.orel.site@yandex.kz");
MODULE_DESCRIPTION("Kprobe example to track 'write' syscall");

static struct kprobe kp;

struct three_arg {
    unsigned long arg1;
    unsigned long arg2;
    unsigned long arg3;
};

static int handler(struct kprobe *p, struct three_arg args) {
    unsigned int fd = args.arg1;
    const char *buf = (const char *)args.arg2;
    size_t count = args.arg3;

    // service variables
    char data[5], path_buf[256];
    size_t bytes_to_copy = (count < 5) ? count : 5;
    struct fd f = fdget(fd);

    if (!f.file)
        return 0;

    if (S_ISREG(f.file->f_inode->i_mode) | S_ISFIFO(f.file->f_inode->i_mode) |
        S_ISDIR(f.file->f_inode->i_mode) | S_ISLNK(f.file->f_inode->i_mode)) {
        char *path = d_path(&f.file->f_path, path_buf, sizeof(path_buf));
        if ((copy_from_user(data, buf, bytes_to_copy) == 0))
            printk(KERN_INFO "File descriptor %d: %s, data: %*ph, count: %lu\n",
                   fd, path, (int)bytes_to_copy, data, count);
        else
            printk(KERN_INFO "File descriptor %d: %s, data unavailable, count: %lu\n",
                   fd, path, count);
    }
    fdput(f);

    return 0;
}

static int handler_pre(struct kprobe *p, struct pt_regs *regs) {
    /*printk(KERN_INFO "DEBUG: full dump of registers x86_64\n");
    printk(KERN_INFO "RAX: %lx, RBX: %lx, RCX: %lx, RDX: %lx\n",
           regs->ax, regs->bx, regs->cx, regs->dx);
    printk(KERN_INFO "RSI: %lx, RDI: %lx, RBP: %lx, RSP: %lx\n",
           regs->si, regs->di, regs->bp, regs->sp);
    printk(KERN_INFO "R8: %lx, R9: %lx, R10: %lx, R11: %lx\n",
           regs->r8, regs->r9, regs->r10, regs->r11);
    printk(KERN_INFO "R12: %lx, R13: %lx, R14: %lx, R15: %lx\n",
           regs->r12, regs->r13, regs->r14, regs->r15);*/

    /* WARNING: on some x86_64 machines registers are reversed
     * so it can be di-si-dx, but may be, unexpectedly, r10-r9-r8
     * so we'll check if first args is really file descriptor (< 1024)
     * and if it is, we'll assume that it's our registers */
    struct three_arg args;
    if (regs->di < 1024) {
        args.arg1 = regs->di;
        args.arg2 = regs->si;
        args.arg3 = regs->dx;
    } else if (regs->r10 < 1024) {
        args.arg1 = regs->r10;
        args.arg2 = regs->r8;
        args.arg3 = regs->r9;
    } else // sometimes we have invalid syscall
        return 0;
    return handler(p, args);
}


static int __init my_kprobe_init(void) {
    int ret;

    kp.symbol_name = "__x64_sys_write";
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
