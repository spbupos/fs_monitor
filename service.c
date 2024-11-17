#include <linux/fs.h>
#include <linux/fs_struct.h>
#include <linux/mount.h>
#include "header.h"

/* for device name resolving */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 17, 0)
#include <linux/blkdev.h>
#else
#include <linux/genhd.h>
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 9, 0)
#include <linux/blk_types.h>
#endif

/* for ring buffer operations */
DEFINE_SPINLOCK(lock);

int kisdigit(char c) {
    return c >= '0' && c <= '9';
}
EXPORT_SYMBOL(kisdigit);

void ring_buffer_init(struct ring_buffer *buffer) {
    buffer->data = kmalloc(BUFFER_SIZE, GFP_KERNEL);
    buffer->head = 0;
    buffer->tail = 0;
    buffer->size = 0;
}
EXPORT_SYMBOL(ring_buffer_init);

void ring_buffer_destroy(struct ring_buffer *buffer) {
    kfree(buffer->data);
    kfree(buffer);
}
EXPORT_SYMBOL(ring_buffer_destroy);

void ring_buffer_clear(struct ring_buffer *buffer) {
    buffer->head = 0;
    buffer->tail = 0;
    buffer->size = 0;
}
EXPORT_SYMBOL(ring_buffer_clear);

void ring_buffer_append(struct ring_buffer *buffer, const char *values, size_t length) {
    size_t i;

    spin_lock(&lock);
    for (i = 0; i < length; i++) {
        if (buffer->size < BUFFER_SIZE) {
            buffer->data[buffer->tail] = values[i];
            buffer->tail = (buffer->tail + 1) % BUFFER_SIZE;
            buffer->size++;
        } else {
            buffer->data[buffer->tail] = values[i];
            buffer->tail = (buffer->tail + 1) % BUFFER_SIZE;
            buffer->head = (buffer->head + 1) % BUFFER_SIZE;
        }
    }
    spin_unlock(&lock);
}
EXPORT_SYMBOL(ring_buffer_append);

void ring_buffer_rread(struct ring_buffer *buffer, char *output) {
    size_t idx = buffer->head, i;
    for (i = 0; i < buffer->size; i++) {
        output[i] = buffer->data[idx];
        idx = (idx + 1) % BUFFER_SIZE;
    }
    output[buffer->size] = '\0';
}
EXPORT_SYMBOL(ring_buffer_rread);

inline int is_regular(struct dentry *dentry) {
    /* any fs without device is considered a service fs
     * yes, we'll lose some fs like NFS or curlftpfs
     * (and some other fuse-based which aren't 'fuseblk')
     * but we're not interested in them
     */
    int subres = (dentry->d_sb->s_type->fs_flags & FS_REQUIRES_DEV);
    if (dentry->d_inode) /* on old kernels inode is sometimes NULL */
        return subres && S_ISREG(dentry->d_inode->i_mode);
    return subres;
}
EXPORT_SYMBOL(is_regular);

// copy 40 bytes from the middle of 'from' to 'to'
int copy_start_middle(char *to, const char *from, size_t count, int middle) {
    size_t write_count, start_pos;

    if (count == 0)
        return 0;

    write_count = count > COPY_BUF_SIZE ? COPY_BUF_SIZE : count;
    start_pos = middle ? (count - write_count) / 2 : 0;
    if (copy_from_user(to, from + start_pos, write_count))
        return 0;

    return (int)write_count;
}
EXPORT_SYMBOL(copy_start_middle);

/* build entry with '\0's as separators */
size_t entry_combiner(char *entry, const char **to_be_entry, size_t cnt) {
    size_t printed_len = 1;
    int i;

    entry[0] = '\0';
    for (i = 0; i < cnt; i++) {
        printed_len += sprintf(entry + printed_len, "%s", to_be_entry[i]);
        entry[printed_len++] = '\0';
    }
    entry[printed_len++] = '\n';

    return printed_len;
}
EXPORT_SYMBOL(entry_combiner);

void free_ptr_array(void **ptr_array, size_t count) {
    size_t i;
    if (ptr_array == NULL)
        return;
    for (i = 0; i < count; i++)
        kfree(ptr_array[i]);
    kfree(ptr_array);
}
EXPORT_SYMBOL(free_ptr_array);

/* reverse buffer */
static int prepend(char **buffer, int *buflen, const char *str, int namelen) {
    *buflen -= namelen;
    if (*buflen < 0)
        return -ENAMETOOLONG;
    *buffer -= namelen;
    memcpy(*buffer, str, namelen);
    return 0;
}

static int prepend_qstr(char **buffer, int *buflen, struct qstr *name) {
    return prepend(buffer, buflen, name->name, name->len);
}

char *own_dentry_path(struct dentry *dentry, char *buf, int buflen) {
    char *end = buf + buflen;
    char *retval;

    if (buflen < 1)
        goto Elong;

    prepend(&end, &buflen, "\0", 1);
    retval = end - 1;
    *retval = '/';

    while (!IS_ROOT(dentry)) {
        struct dentry *parent = dentry->d_parent;

        prefetch(parent);
        if ((prepend_qstr(&end, &buflen, &dentry->d_name) != 0) ||
            (prepend(&end, &buflen, "/", 1) != 0))
            goto Elong;

        retval = end;
        dentry = parent;
    }

    return retval;

Elong:
    return ERR_PTR(-ENAMETOOLONG);
}
EXPORT_SYMBOL(own_dentry_path);

/* device name resolver */
void own_bdevname(struct block_device *bdev, char *buf) {
	struct gendisk *hd = bdev->bd_disk;
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 14, 0)
    int partno = bdev->bd_part->partno;
#elif LINUX_VERSION_CODE < KERNEL_VERSION(6, 10, 0)
	int partno = bdev->bd_partno;
#else
    int partno = bdev->__bd_flags & BD_PARTNO;
#endif

	if (!partno)
		sprintf(buf, "/dev/%s", hd->disk_name);
	else if (kisdigit(hd->disk_name[strlen(hd->disk_name)-1]))
		sprintf(buf, "/dev/%sp%d", hd->disk_name, partno);
	else
		sprintf(buf, "/dev/%s%d", hd->disk_name, partno);
}
EXPORT_SYMBOL(own_bdevname);
