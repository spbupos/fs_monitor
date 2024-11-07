#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fsnotify.h>
#include <linux/init.h>
#include <linux/timekeeping.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/debugfs.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("Extended R/W access monitoring module");
MODULE_VERSION("0.1.0");


// global structs for monitoring
static struct dentry *output_dentry;
static struct fsnotify_group *monitor_group;

// Buffer to store logs for the virtual file (used in Step 4)
#define MAX_LOG_ENTRIES 4096
#define LOG_ENTRY_SIZE 512
static char log_buffer[MAX_LOG_ENTRIES * LOG_ENTRY_SIZE];
static size_t log_index = 0;

// name of file in proc
#define PROC_FILE_NAME "fs_monitor"


static void log_file_event(struct inode *inode, const char *full_path, size_t file_size) {
    char dev_path[32];
    struct timespec64 ts;
    ssize_t bytes_read;
    int operation_status = 0;
    char file_content[100] = {0}; // Buffer for the portion of file content

    // Get current timestamp in ns
    ktime_get_real_ts64(&ts);
    s64 timestamp_ns = timespec64_to_ns(&ts);

    // Get device path (for example, /dev/sda1)
    snprintf(dev_path, sizeof(dev_path), "/dev/%s", inode->i_sb->s_id);

    // Read the middle part of the file
    if (file_size > 20) {
        loff_t offset = file_size > 200 ? file_size / 2 - 50 : 10; // Middle of file or fixed for small files
        size_t read_size = file_size > 200 ? 100 : 20;
        bytes_read = kernel_read(inode, offset, file_content, read_size);
    } else {
        bytes_read = kernel_read(inode, 0, file_content, file_size);
    }

    // Check if reading was successful
    if (bytes_read < 0) {
        operation_status = bytes_read; // Store error code
        bytes_read = 0;                // No data read
    }

    // 4. Store log data in binary format in log_buffer
    if (log_index < MAX_LOG_ENTRIES) {
        snprintf(log_buffer + log_index * LOG_ENTRY_SIZE, LOG_ENTRY_SIZE,
                 "Timestamp(ns): %lld\nDevice: %s\nPath: %s\nData: %.*s\nStatus: %d\n\n",
                 timestamp_ns, dev_path, full_path, (int)bytes_read, file_content, operation_status);
        log_index++;
    }

}


static int monitor_event_handler(struct fsnotify_mark *mark, struct inode *inode,
                                 u32 mask, const void *data, int data_type) {
    // Filter for create or modify events
    if (mask & FS_CREATE || mask & FS_MODIFY) {
        struct dentry *dentry = d_find_alias(inode); // Get dentry for full path
        char full_path[PATH_MAX];

        if (dentry) {
            dentry_path_raw(dentry, full_path, PATH_MAX); // Get full path
            printk(KERN_INFO "File accessed: %s\n", full_path);

            log_file_event(inode, full_path, i_size_read(inode)); // Call log_file_event
            dput(dentry);
        }
    }
    return 0;
}


static const struct fsnotify_ops fsnotify_ops = {
        .handle_event = monitor_event_handler,
};


// Function to add a mark to a mounted filesystem
static int add_fsnotify_mark(struct vfsmount *mnt) {
    struct fsnotify_mark *mark;

    mark = fsnotify_alloc_mark(&monitor_group->mark_mutex);
    if (!mark) {
        printk(KERN_ERR "Failed to allocate fsnotify mark\n");
        return -ENOMEM;
    }

    // Set up the mark to monitor a specific mount point (vfsmount)
    fsnotify_init_mark(mark, monitor_group);
    fsnotify_set_mark_mount(mark, mnt);

    // Add mark to the group
    if (fsnotify_add_mark(mark, NULL, 0)) {
        printk(KERN_ERR "Failed to add fsnotify mark\n");
        fsnotify_put_mark(mark);
        return -1;
    }

    printk(KERN_INFO "Added fsnotify mark to monitor %s\n", mnt->mnt_devname);
    return 0;
}


// Read function to output `log_buffer` to user space
static ssize_t proc_read_log(struct file *file, char __user *buf, size_t len, loff_t *ppos) {
    size_t log_size = log_index * LOG_ENTRY_SIZE;
    ssize_t ret;

    // Ensure we're not trying to read beyond the buffer
    if (*ppos >= log_size)
        return 0;

    // Copy data from kernel space to user space
    ret = simple_read_from_buffer(buf, len, ppos, log_buffer, log_size);

    return ret;
}

// Define file operations for the proc entry
static const struct file_operations proc_file_ops = {
    .owner = THIS_MODULE,
    .read = proc_read_log,
};


static int __init fs_monitor_init(void) {
    struct vfsmount *mnt;
    struct list_head *p;
    printk(KERN_INFO "Initializing FS Monitor Module\n");

    // Set up your device or proc file for logging data
    if (!proc_create(PROC_FILE_NAME, 0444, NULL, &proc_file_ops)) {
        printk(KERN_ERR "Failed to create /proc/%s\n", PROC_FILE_NAME);
        return -ENOMEM;
    }
    printk(KERN_INFO "/proc/%s created for file monitoring logs\n", PROC_FILE_NAME);

    monitor_group = fsnotify_alloc_group(&fsnotify_ops);
    if (IS_ERR(monitor_group)) {
        printk(KERN_ERR "Failed to create FSNotify group\n");
        return PTR_ERR(monitor_group);
    }

    // Iterate over all mounted filesystems and add marks
    list_for_each(p, &current->nsproxy->mnt_ns->list) {
        mnt = list_entry(p, struct vfsmount, mnt_list);

        // Skip special filesystems (e.g., proc, sysfs, tmpfs)
        if (strcmp(mnt->mnt_devname, "proc") == 0 ||
            strcmp(mnt->mnt_devname, "sysfs") == 0 ||
            strcmp(mnt->mnt_devname, "tmpfs") == 0) {
            continue;
            }

        // Add a mark to this mount point
        add_fsnotify_mark(mnt);
    }

    printk(KERN_INFO "FS Monitor Module Initialized\n");
    return 0;
}


static void __exit fs_monitor_exit(void) {
    printk(KERN_INFO "Exiting FS Monitor Module\n");

    // Clean up
    debugfs_remove(output_dentry);
    fsnotify_destroy_group(monitor_group);
}


module_init(fs_monitor_init);
module_exit(fs_monitor_exit);
