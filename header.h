#ifndef VARS_H
#define VARS_H

// global structs for monitoring
static struct fsnotify_group *monitor_group;

// Buffer to store logs for the virtual file (used in Step 4)
#define MAX_LOG_ENTRIES 4096
#define LOG_ENTRY_SIZE 512
static char log_buffer[MAX_LOG_ENTRIES * LOG_ENTRY_SIZE];
static size_t log_index = 0;

// name of file in proc
#define PROC_FILE_NAME "fs_monitor"

// proc dir entry for deletion at close
static struct proc_dir_entry *proc_entry;

#endif // VARS_H
