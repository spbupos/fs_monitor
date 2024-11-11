#include <linux/module.h>
#include <linux/kprobes.h>
#include <linux/uaccess.h>
#include <linux/printk.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kernel Guru Award Winner");
MODULE_DESCRIPTION("Kprobe Example to Track 'write' Syscall with First 5 Bytes of Data");

static struct kprobe kp;

static int handler_pre(struct kprobe *p, struct pt_regs *regs) {
    // Extract the arguments from the registers
    // 1 - di, 2 - si, 3 - dx
    unsigned int fd = (regs->di >> 12) & 0xFFFF;
    const char *buf = (const char *)regs->si;
    size_t count = regs->dx;

    // service variables
    char data[5];
    size_t bytes_to_copy = (count < 5) ? count : 5;

    if (regs->si < 0x80)
        return 0;

    if (copy_from_user(data, buf, bytes_to_copy) == 0)
        printk(KERN_INFO "Write syscall - FD: %d, Data (hex): %*ph, count: %lx, si: %lx\n",
               fd, (int)bytes_to_copy, data, count, regs->si);
    else
        printk(KERN_INFO "Write syscall - FD: %d, Failed to read buffer, count: %lx, si: %lx\n",
               fd, count, regs->si);

    return 0;
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
