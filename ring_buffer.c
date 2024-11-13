#include <linux/module.h>
#include "header.h"

void ring_buffer_init(struct ring_buffer *buffer) {
    buffer->head = 0;
    buffer->tail = 0;
    buffer->size = 0;
}
EXPORT_SYMBOL(ring_buffer_init);

void ring_buffer_append(struct ring_buffer *buffer, const char *values, size_t length) {
    for (size_t i = 0; i < length; i++) {
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
    size_t idx = buffer->head;
    for (size_t i = 0; i < buffer->size; i++) {
        output[i] = buffer->data[idx];
        idx = (idx + 1) % BUFFER_SIZE;
    }
    output[buffer->size] = '\0';
}

ssize_t proc_read(struct file *file, char __user *buffer, size_t count, loff_t *pos) {
    char *out_buffer = kmalloc(BUFFER_SIZE + 1, GFP_KERNEL);
    if (*pos > 0)
        return 0;

    ring_buffer_read(&rbuf, out_buffer);
    if (copy_to_user(buffer, out_buffer, rbuf.size))
        return -EFAULT;

    *pos = rbuf.size;
    kfree(out_buffer);
    return rbuf.size;
}
EXPORT_SYMBOL(proc_read);
