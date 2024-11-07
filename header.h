#ifndef VARS_H
#define VARS_H

// global structs for monitoring
static struct fsnotify_group *monitor_group;

// Buffer to store logs for the virtual file (used in Step 4)
// WARNING: max size of proc buffer is 262144 bytes
#define LOG_ENTRY_SIZE 512
#define LOG_MAX_ENTRIES 512
static char *log_buffer;
static int entry_count = 0;

// name of file in proc
#define PROC_FILE_NAME "fs_monitor"

// proc dir entry for deletion at close
static struct proc_dir_entry *proc_entry;

// mark
static struct fsnotify_mark *mark;

#endif // VARS_H
