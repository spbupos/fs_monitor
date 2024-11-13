#ifndef VARS_H
#define VARS_H

#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/proc_fs.h>

#define COPY_BUF_SIZE 40
#define BASE64_ENCODED_MAX 60
#define ENTRY_SIZE 512
#define BUFFER_SIZE 262144
#define MAX_PATH_LEN 256

struct ring_buffer {
    char *data;
    size_t head, tail, size;
};
extern struct ring_buffer rbuf;
void ring_buffer_init(struct ring_buffer *buffer);
void ring_buffer_append(struct ring_buffer *buffer, const char *values, size_t length);

ssize_t proc_read(struct file *file, char __user *buffer, size_t count, loff_t *pos);
static const struct proc_ops proc_fops = {
    .proc_read = proc_read,
};

#endif // VARS_H
