#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <string.h>
#include <sys/ioctl.h>

#include "ioctl.h"

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <file_path>\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *file_path = argv[1];
    int fd = open(file_path, O_RDONLY);
    if (fd < 0) {
        perror("Error : opening file");
        return EXIT_FAILURE;
    }


    char buf[1024];
    if(ioctl(fd, USED_BLOCKS, buf) == -1)
        perror("\n");
    printf("USED_BLOCKS : %s\n", buf);
    if(ioctl(fd, PARTIAL_BLOCKS, buf) == -1)
        perror("\n");
    printf("PARTIAL_BLOCKS : %s\n", buf);
    if(ioctl(fd, INTERNAL_FRAG, buf) == -1)
        perror("\n");
    printf("INTERNAL_FRAG : %s\n", buf);
    if(ioctl(fd, USED_BLOCKS_INFO, buf) == -1)
        perror("\n");

    printf("USED_BLOCKS_INFO : %s\n", buf);
    

    close(fd);
    return 0;
}