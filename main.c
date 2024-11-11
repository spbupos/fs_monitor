#include <linux/module.h>
#include <linux/kprobes.h>
#include <linux/uaccess.h>
#include <linux/printk.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kernel Guru Award Winner");
MODULE_DESCRIPTION("Kprobe Example to Track 'write' Syscall with First 5 Bytes of Data");

static struct kprobe kp;

// Pre-handler for the probe
static int handler_pre(struct kprobe *p, struct pt_regs *regs) {
    unsigned int fd = regs->di;                  // File descriptor
    const char __user *buf = (const char __user *)regs->si; // Buffer pointer
    size_t count = regs->dx;             // Number of bytes to write
    char data[5];                        // Local buffer for the first 5 bytes

    // Check if there are at least 5 bytes to read, otherwise adjust
    size_t bytes_to_copy = (count < 5) ? count : 5;

    // Copy the first 5 bytes from the user-space buffer
    if (copy_from_user(data, buf, bytes_to_copy) == 0) {
        // Print each byte in hex format
        printk(KERN_INFO "Write syscall - FD: %d, Data (hex): %*ph, count: %lu\n",
               fd, (int)bytes_to_copy, data, count);
    } else {
        printk(KERN_INFO "Write syscall - FD: %d, Failed to read buffer, count: %lu\n",
               fd, count);
    }

    return 0;
}

// Module initialization function
static int __init my_kprobe_init(void) {
    int ret;

    // Set the address to hook; use `__x64_sys_write` for x86_64 architecture
    kp.symbol_name = "__x64_sys_write";
    kp.pre_handler = handler_pre;

    // Register the Kprobe
    ret = register_kprobe(&kp);
    if (ret < 0) {
        printk(KERN_INFO "Failed to register kprobe: %d\n", ret);
        return ret;
    }
    printk(KERN_INFO "Kprobe registered for write syscall\n");
    return 0;
}

// Module cleanup function
static void __exit my_kprobe_exit(void) {
    unregister_kprobe(&kp);
    printk(KERN_INFO "Kprobe unregistered\n");
}

module_init(my_kprobe_init);
module_exit(my_kprobe_exit);
