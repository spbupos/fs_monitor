#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <errno.h>
#include <string.h>

#define EVENT_BUF_LEN 1024

void print_with_zeros(const char *string, int len) {
}

int main(int argc, char **argv) {
    struct pollfd fds;
    char buf[EVENT_BUF_LEN];
    int fd;

    // Open the device
    if (argc != 2) {
        printf("Usage: %s <fs_monitor_chardev>\n", argv[0]);
        return 1;
    }

    fd = open(argv[1], O_RDONLY);
    if (fd == -1) {
        perror("open");
        return EXIT_FAILURE;
    }

    fds.fd = fd;
    fds.events = POLLIN;

    while (1) {
        int poll_num = poll(&fds, 1, -1);
        if (poll_num == -1) {
            perror("poll");
            close(fd);
            return EXIT_FAILURE;
        }

        if (fds.revents & POLLIN) {
            ssize_t len = read(fd, buf, EVENT_BUF_LEN);
            if (len < 0) {
                perror("read");
                close(fd);
                return EXIT_FAILURE;
            }
            write(1, buf, len); // write to stdout instead of printf because our string contains '\0's
        }
    }

    close(fd);
    return EXIT_SUCCESS;
}
