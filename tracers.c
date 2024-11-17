#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/fs.h>
#include "header.h"

#if LINUX_VERSION_CODE > KERNEL_VERSION(6, 0, 0)
#include <linux/base64.h>
#endif

bool data_available = false;
char monitor_entry[ENTRY_SIZE];

static inline struct inode *get_file_inode(struct file *file) {
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 9, 0)
    return file->f_path.dentry->d_inode;
#else
    return file->f_inode; /* inode field was added at 3.9-rc1 */
#endif
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 17, 0)
#include <linux/hrtimer.h>
#include <linux/ktime.h>
static inline s64 ktime_get_ns(void) {
    return ktime_to_ns(ktime_get()); /* 'ktime_get_ns' was added with 'linux/timekeeping.h' header in 3.17-rc1 */
}
#endif

/* in x86_64 registers is used for arguments passing: rdi, rsi, rdx, rcx, r8, r9
 * but in 'struct pt_regs' we sometimes actually have r10, r9, r8, ... (???)
 */
int vfs_write_trace(struct kprobe *p, struct pt_regs *regs) {
    /* taken from declaration of 'vfs_write' function
     * ssize_t vfs_write(struct file *file, const char __user *buf, size_t count, loff_t *pos)
     */
    struct file *file = (struct file *)regs->di;
    const char *buf = (const char *)regs->si;
    size_t count = (size_t)regs->dx;
    loff_t *ppos = (loff_t *)regs->cx;

    /* some buffers */
    char kbuf[COPY_BUF_SIZE],
         filename[MAX_PATH_LEN],
         *path;

    int write_count;
    size_t r, entry_current_size = 0;
    loff_t pos = ppos ? *ppos : 0;

    char **to_be_entry;

    /* we want work only with writes on real files on real FS */
    if (!file || !is_regular(file->f_path.dentry))
        return 0;

    to_be_entry = kmalloc(ENTRY_MAX_CNT_SIZE * sizeof(char *), GFP_KERNEL);
    memset(to_be_entry, 0, ENTRY_MAX_CNT_SIZE * sizeof(char *));

    /* clean up global entry by memset */
    memset(monitor_entry, 0, ENTRY_SIZE);

    /* timestamp */
    to_be_entry[entry_current_size] = kmalloc(SPEC_STRINGS_SIZE, GFP_KERNEL);
    sprintf(to_be_entry[entry_current_size++], "%lld", ktime_get_ns());

    /* file path */
    path = d_path(&file->f_path, filename, MAX_PATH_LEN);
    /* WARNING: path is actually filename + some_offset, so
     * it can't be managed or kfreed, so we need to kmalloc it
     */
    to_be_entry[entry_current_size] = kmalloc(strlen(path) + 1, GFP_KERNEL);
    sprintf(to_be_entry[entry_current_size++], "%s", path);

    /* middle data */
    write_count = copy_start_middle(kbuf, buf, count, 1);
    to_be_entry[entry_current_size] = kmalloc(BASE64_ENCODED_MAX, GFP_KERNEL);
    r = base64_encode((const u8 *)kbuf, write_count, to_be_entry[entry_current_size]);
    to_be_entry[entry_current_size++][r] = '\0';

    /* file size */
    to_be_entry[entry_current_size] = kmalloc(SPEC_STRINGS_SIZE, GFP_KERNEL);
    sprintf(to_be_entry[entry_current_size++], "%lld",
            max(pos + (loff_t)count, get_file_inode(file)->i_size));

    /* beginning data */
    to_be_entry[entry_current_size] = kmalloc(BASE64_ENCODED_MAX, GFP_KERNEL);
    if (pos == 0) {
        write_count = copy_start_middle(kbuf, buf, count, 0);
        r = base64_encode((const u8 *) kbuf, write_count, to_be_entry[entry_current_size]);
        to_be_entry[entry_current_size++][r] = '\0';
    } else
        sprintf(to_be_entry[entry_current_size++], "<not_a_beginning>");

    /* write entry to ring buffer */
    r = entry_combiner(monitor_entry, (const char **)to_be_entry, entry_current_size);
    ring_buffer_append(rbuf, monitor_entry, r);

    /* cleanup and wake up poll */
    free_ptr_array((void **)to_be_entry, entry_current_size);
    if (!data_available) {
        data_available = true;
        wake_up_interruptible(&wait_queue);
    }

    return 0;
}
EXPORT_SYMBOL(vfs_write_trace);

int vfs_unlink_trace(struct kprobe *p, struct pt_regs *regs) {
#if LINUX_VERSION_CODE > KERNEL_VERSION(5, 11, 0)
    /* taken from declaration of 'do_unlinkat' function
     * int vfs_unlink(struct user_namespace *mnt_userns, struct inode *dir,
           struct dentry *dentry, struct inode **delegated_inode)
     * first argument is varied on versions 5.12-6.12, but we don't
     * need it at all, so it will be 'void *dummy' */
    void *dummy = (void *)regs->di;
    struct inode *dir = (struct inode *)regs->si;
    struct dentry *dentry = (struct dentry *)regs->dx;
    struct inode **delegated_inode = (struct inode **)regs->r10;
#else
    /* taken from declaration of 'do_unlinkat' function
     * int vfs_unlink(struct inode *dir, struct dentry *dentry,
           struct inode **delegated_inode) */
    struct inode *dir = (struct inode *)regs->di;
    struct dentry *dentry = (struct dentry *)regs->si;
    struct inode **delegated_inode = (struct inode **)regs->dx;
#endif
    char *path, path_buf[MAX_PATH_LEN];
    size_t r, entry_current_size = 0;

    char **to_be_entry;

    if (!dentry || !is_regular(dentry))
        return 0;

    to_be_entry = kmalloc(ENTRY_MAX_CNT_SIZE * sizeof(char *), GFP_KERNEL);
    memset(to_be_entry, 0, ENTRY_MAX_CNT_SIZE * sizeof(char *));

    /* clean up global entry by memset */
    memset(monitor_entry, 0, ENTRY_SIZE);

    /* timestamp */
    to_be_entry[entry_current_size] = kmalloc(SPEC_STRINGS_SIZE, GFP_KERNEL);
    sprintf(to_be_entry[entry_current_size++], "%lld", ktime_get_ns());

    /* device name */
    to_be_entry[entry_current_size] = kmalloc(MAX_PATH_LEN, GFP_KERNEL);
    own_bdevname(dentry->d_sb->s_bdev, to_be_entry[entry_current_size++]);

    /* file path */
    path = own_dentry_path(dentry, path_buf, MAX_PATH_LEN);
    to_be_entry[entry_current_size] = kmalloc(strlen(path) + 1, GFP_KERNEL);
    sprintf(to_be_entry[entry_current_size++], "%s", path);

    /* deleted flag */
    to_be_entry[entry_current_size] = kmalloc(SPEC_STRINGS_SIZE, GFP_KERNEL);
    sprintf(to_be_entry[entry_current_size++], "<deleted>");

    /* write entry to ring buffer */
    r = entry_combiner(monitor_entry, (const char **)to_be_entry, entry_current_size);
    ring_buffer_append(rbuf, monitor_entry, r);

    /* cleanup and wake up poll */
    free_ptr_array((void **)to_be_entry, entry_current_size);
    if (!data_available) {
        data_available = true;
        wake_up_interruptible(&wait_queue);
    }

    return 0;
}
EXPORT_SYMBOL(vfs_unlink_trace);

int vfs_rename_trace(struct kprobe *p, struct pt_regs *regs) {
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 12, 0)
    /* int vfs_rename(struct inode *old_dir, struct dentry *old_dentry,
                      struct inode *new_dir, struct dentry *new_dentry,
                      ...) */
#else
    /* int vfs_rename(struct renamedata *rd) */
#endif
    TODO();

    return 0;
}
EXPORT_SYMBOL(vfs_rename_trace);

int vfs_copy_trace(struct kprobe *p, struct pt_regs *regs) {
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 5, 0)
    /* ssize_t do_sendfile(int out_fd, int in_fd, loff_t *ppos,
                           size_t count, ...) */
#else
    /* ssize_t vfs_copy_file_range(struct file *file_in, loff_t pos_in,
			                       struct file *file_out, loff_t pos_out,
                                   ...) */
#endif
    TODO();

    return 0;
}

EXPORT_SYMBOL(vfs_copy_trace);
