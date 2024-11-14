#include "header.h"

#if LINUX_VERSION_CODE > KERNEL_VERSION(6, 0, 0)
#include <linux/base64.h>
#endif

int data_available = 0;

/* in x86_64 registers is used for arguments passing: rdi, rsi, rdx, rcx
 * WARNING: in some cases can be transformed to r10, r9, r8, rdx
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
            entry[ENTRY_SIZE], *path;

    int write_count;
    size_t r;
    loff_t pos = ppos ? *ppos : 0;

    char **to_be_entry;

    /* we want work only with writes on real files on real FS */
    if (!file || !S_ISREG(file->f_inode->i_mode) || is_service_fs(&file->f_path))
        return 0;

    to_be_entry = kmalloc(ENTRY_WRITE_LENGTH * sizeof(char *), GFP_KERNEL);

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
    r = entry_combiner(entry, (const char **)to_be_entry, ENTRY_WRITE_LENGTH);
    ring_buffer_append_both(entry, r);
    data_available = 1;

    /* cleanup */
    free_ptr_array((void **)to_be_entry, ENTRY_WRITE_LENGTH);
    wake_up_interruptible(&wait_queue);

    return 0;
}
EXPORT_SYMBOL(vfs_write_trace);

int do_unlinkat_trace(struct kprobe *p, struct pt_regs *regs) {
    /* taken from declaration of 'do_unlinkat' function
     * int do_unlinkat(int dfd, struct filename *name)
     */
    struct filename *name = (struct filename *)regs->si;

    /* some buffers */
    char entry[ENTRY_SIZE], filename[MAX_PATH_LEN];
    char *parent_path;
    size_t r;

    struct path parent;
    struct qstr last;
    int type, ret;

    char **to_be_entry = kmalloc(ENTRY_DELETE_LENGTH * sizeof(char *), GFP_KERNEL);

    /* timestamp */
    to_be_entry[0] = kmalloc(SPEC_STRINGS_SIZE, GFP_KERNEL);
    sprintf(to_be_entry[0], "%lld", ktime_get_ns());

    /* file path */
    to_be_entry[1] = kmalloc(MAX_PATH_LEN, GFP_KERNEL);
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 4, 0)
    sprintf(to_be_entry[1], "%s", name->name);
#else
    ret = vfs_path_parent_lookup(name, 0, &parent, &last, &type, NULL);
    if (ret < 0 || is_service_fs(&parent))
        return 0;
    parent_path = d_path(&parent, filename, MAX_PATH_LEN);
    sprintf(to_be_entry[1], "%s/%.*s", parent_path, last.len, last.name);
#endif

    /* "deleted" message */
    to_be_entry[2] = kmalloc(SPEC_STRINGS_SIZE, GFP_KERNEL);
    sprintf(to_be_entry[2], "<deleted>");

    /* write entry to ring buffer */
    r = entry_combiner(entry, (const char **)to_be_entry, ENTRY_DELETE_LENGTH);
    ring_buffer_append_both(entry, r);
    data_available = 1;

    /* cleanup */
    free_ptr_array((void **)to_be_entry, ENTRY_DELETE_LENGTH);
    wake_up_interruptible(&wait_queue);

    return 0;
}
EXPORT_SYMBOL(do_unlinkat_trace);
