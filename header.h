#ifndef VARS_H
#define VARS_H

#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/proc_fs.h>
#include <linux/version.h>
#include <linux/namei.h>
#include <linux/slab.h>


/* init */
#define KPROBES_COUNT 2

extern struct kprobe **kp;


/* buffers */
#define COPY_BUF_SIZE 40
#define BASE64_ENCODED_MAX 60


/* ring buffer */
#define ENTRY_SIZE 512
#define ENTRY_WRITE_LENGTH 5
#define ENTRY_DELETE_LENGTH 3
#define SPEC_STRINGS_SIZE 30

struct ring_buffer {
    char *data;
    size_t head, tail, size;
};
extern struct ring_buffer *rbuf;

void ring_buffer_init(struct ring_buffer *buffer);
void ring_buffer_destroy(struct ring_buffer *buffer);
void ring_buffer_clear(struct ring_buffer *buffer);
void ring_buffer_read(struct ring_buffer *buffer, char *output);
void ring_buffer_append(struct ring_buffer *buffer, const char *values, size_t length);


/* chardev */
#define BUFFER_SIZE 131072
#define MAX_PATH_LEN 512

#define DEVNAME "fs_monitor"
#define CLASS_NAME "tracer_class"
#define DEVMODE 0444

ssize_t chardev_read(struct file *file, char __user *buffer, size_t count, loff_t *pos);


/* backward compatibility */
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 0, 0)
#define BASE64_CHARS(nbytes)   DIV_ROUND_UP((nbytes) * 4, 3)

int base64_encode(const u8 *src, int len, char *dst);
int base64_decode(const char *src, int len, u8 *dst);
#endif


/* service */
int is_service_fs(struct path *path);

int copy_start_middle(char *to, const char *from, size_t count, int middle);
size_t entry_combiner(char *entry, const char **to_be_entry, size_t cnt);
void free_ptr_array(void **ptr_array, size_t count);


/* tracers */
int vfs_write_trace(struct kprobe *p, struct pt_regs *regs);
int do_unlinkat_trace(struct kprobe *p, struct pt_regs *regs);

extern bool data_available;
//extern spinlock_t lock;
extern char monitor_entry[ENTRY_SIZE];


/* poll */
extern struct wait_queue_head wait_queue;

#endif // VARS_H
