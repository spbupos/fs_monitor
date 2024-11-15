#include "header.h"

#if LINUX_VERSION_CODE > KERNEL_VERSION(6, 0, 0)
#include <linux/base64.h>
#endif

bool data_available = false;
char monitor_entry[ENTRY_SIZE];

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
    size_t r;
    loff_t pos = ppos ? *ppos : 0;

    char **to_be_entry;

    /* we want work only with writes on real files on real FS */
    if (!file || !S_ISREG(file->f_inode->i_mode) || is_service_fs(file->f_path.dentry))
        return 0;

    to_be_entry = kmalloc(ENTRY_WRITE_LENGTH * sizeof(char *), GFP_KERNEL);

    /* clean up global entry by memset */
    memset(monitor_entry, 0, ENTRY_SIZE);

    /* timestamp */
    to_be_entry[0] = kmalloc(SPEC_STRINGS_SIZE, GFP_KERNEL);
    sprintf(to_be_entry[0], "%lld", ktime_get_ns());

    /* file path */
    path = d_path(&file->f_path, filename, MAX_PATH_LEN);
    /* WARNING: path is actually filename + some_offset, so
     * it can't be managed or kfreed, so we need to kmalloc it
     */
    to_be_entry[1] = kmalloc(strlen(path) + 1, GFP_KERNEL);
    sprintf(to_be_entry[1], "%s", path);

    /* middle data */
    write_count = copy_start_middle(kbuf, buf, count, 1);
    to_be_entry[2] = kmalloc(BASE64_ENCODED_MAX, GFP_KERNEL);
    r = base64_encode((const u8 *)kbuf, write_count, to_be_entry[2]);
    to_be_entry[2][r] = '\0';

    /* file size */
    to_be_entry[3] = kmalloc(SPEC_STRINGS_SIZE, GFP_KERNEL);
    sprintf(to_be_entry[3], "%lld",
            max(pos + (loff_t)count, file->f_inode->i_size));

    /* beginning data */
    to_be_entry[4] = kmalloc(BASE64_ENCODED_MAX, GFP_KERNEL);
    write_count = copy_start_middle(kbuf, buf, count, 0);
    r = base64_encode((const u8 *)kbuf, write_count, to_be_entry[4]);
    to_be_entry[4][r] = '\0';

    /* write entry to ring buffer */
    r = entry_combiner(monitor_entry, (const char **)to_be_entry, ENTRY_WRITE_LENGTH);
    ring_buffer_append(rbuf, monitor_entry, r);

    /* cleanup and wake up poll */
    free_ptr_array((void **)to_be_entry, ENTRY_WRITE_LENGTH);
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
    size_t r;

    char **to_be_entry;

    if (!dentry || !S_ISREG(dentry->d_inode->i_mode) || is_service_fs(dentry))
        return 0;

    to_be_entry = kmalloc(ENTRY_DELETE_LENGTH * sizeof(char *), GFP_KERNEL);

    /* clean up global entry by memset */
    memset(monitor_entry, 0, ENTRY_SIZE);

    /* timestamp */
    to_be_entry[0] = kmalloc(SPEC_STRINGS_SIZE, GFP_KERNEL);
    sprintf(to_be_entry[0], "%lld", ktime_get_ns());

    /* file path */
    path = dentry_path_raw(dentry, path_buf, MAX_PATH_LEN);
    to_be_entry[1] = kmalloc(strlen(path) + 1, GFP_KERNEL);
    sprintf(to_be_entry[1], "%s", path);

    /* deleted flag */
    to_be_entry[2] = kmalloc(SPEC_STRINGS_SIZE, GFP_KERNEL);
    sprintf(to_be_entry[2], "<deleted>");

    /* write entry to ring buffer */
    r = entry_combiner(monitor_entry, (const char **)to_be_entry, ENTRY_DELETE_LENGTH);
    ring_buffer_append(rbuf, monitor_entry, r);

    /* cleanup and wake up poll */
    free_ptr_array((void **)to_be_entry, ENTRY_DELETE_LENGTH);
    if (!data_available) {
        data_available = true;
        wake_up_interruptible(&wait_queue);
    }

    return 0;
}
EXPORT_SYMBOL(vfs_unlink_trace);
