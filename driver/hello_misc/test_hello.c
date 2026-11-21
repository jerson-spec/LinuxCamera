#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <string.h>
#include <sys/ioctl.h>

#define HELLO_IOC_MAGIC   'h'
#define HELLO_IOC_RESET   _IO(HELLO_IOC_MAGIC, 0)

int main(void)
{
    int fd = open("/dev/hello_misc", O_RDWR);
    char buf[64] = {0};
    struct pollfd pfd;
    int ret;

    if (fd < 0) {
        perror("open");
        return 1;
    }

    ioctl(fd, HELLO_IOC_RESET);    

    pfd.fd = fd;
    pfd.events = POLLIN;

    printf("polling...\n");
    ret = poll(&pfd, 1, 5000);       /* 最多等 5 秒 */
    if (ret < 0) {
        perror("poll");
    } else if (ret == 0) {
        printf("timeout\n");
    } else if (pfd.revents & POLLIN) {
        read(fd, buf, sizeof(buf) - 1);
        printf("got: %s\n", buf);
    }

    close(fd);
    return 0;
}


