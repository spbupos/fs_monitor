#include <linux/fs.h>
#include "header.h"

struct file_system_type *proc_fs, *sysfs_fs, *devtmpfs_fs, *tmpfs_fs, *ramfs_fs;

void ring_buffer_init(struct ring_buffer *buffer) {
    buffer->head = 0;
    buffer->tail = 0;
    buffer->size = 0;
}
EXPORT_SYMBOL(ring_buffer_init);

void ring_buffer_append(struct ring_buffer *buffer, const char *values, size_t length) {
    size_t i;
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
}
EXPORT_SYMBOL(ring_buffer_append);

void ring_buffer_read(struct ring_buffer *buffer, char *output) {
    size_t idx = buffer->head, i;
    for (i = 0; i < buffer->size; i++) {
        output[i] = buffer->data[idx];
        idx = (idx + 1) % BUFFER_SIZE;
    }
    output[buffer->size] = '\0';
}
EXPORT_SYMBOL(ring_buffer_read);

int init_filesystem_pointers(void) {
    proc_fs = get_fs_type("proc");
    sysfs_fs = get_fs_type("sysfs");
    devtmpfs_fs = get_fs_type("devtmpfs");
    tmpfs_fs = get_fs_type("tmpfs");
    ramfs_fs = get_fs_type("ramfs");

    if (!proc_fs || !sysfs_fs || !devtmpfs_fs || !tmpfs_fs || !ramfs_fs) {
        pr_err("Error initializing filesystem pointers\n");
        return -ENODEV;
    }

    return 0;
}
EXPORT_SYMBOL(init_filesystem_pointers);

int is_service_fs_dentry(struct dentry *dentry) {
    struct super_block *sb = dentry->d_sb;

    if (sb->s_type == proc_fs ||
        sb->s_type == sysfs_fs ||
        sb->s_type == devtmpfs_fs ||
        sb->s_type == tmpfs_fs ||
        sb->s_type == ramfs_fs)
        return 1;

    return 0;
}
EXPORT_SYMBOL(is_service_fs_dentry);

int is_service_fs(struct file *file) {
    return is_service_fs_dentry(file->f_path.dentry);
}
EXPORT_SYMBOL(is_service_fs);

// copy 40 bytes from the middle of 'from' to 'to'
int copy_middle(char *to, const char *from, size_t count) {
    if (count == 0)
        return 0;

    size_t write_count = count > COPY_BUF_SIZE ? COPY_BUF_SIZE : count;
    size_t start_pos = (count - write_count) / 2;
    if (copy_from_user(to, from + start_pos, write_count))
        return 0;

    return (int)write_count;
}
EXPORT_SYMBOL(copy_middle);

// build entry with '\0's as separators
size_t entry_combiner(char *entry,
                      const char *s1, size_t s1_len,
                      const char *s2, size_t s2_len) {
    entry[0] = '\0';
    sprintf(entry + 1, "%s", s1);
    entry[s1_len + 1] = '\0';
    sprintf(entry + s1_len + 2, "%s", s2);
    entry[s1_len + s2_len + 2] = '\0';
    entry[s1_len + s2_len + 3] = '\n';
    return s1_len + s2_len + 4;
}
EXPORT_SYMBOL(entry_combiner);
