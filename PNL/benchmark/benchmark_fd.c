#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#define FOLDER "./ouichefs" // folder of ouichefs
#define FILESIZE_PACE 100000 // pace for file size
#define POSITION_PACE 10 // % pace for position in the file

#define FILESIZE_MAX 1024 * 1024 - 1
#define WRITE_DATA "This a Hello World I/O test in ouichefs"

ssize_t read_line(int fd, char *buffer, size_t size) {
    size_t total_bytes_read = 0;
    while (total_bytes_read < size - 1) {
        ssize_t bytes_read = read(fd, buffer + total_bytes_read, 1);
        if (bytes_read == -1) {
            perror("read");
            return -1; 
        }
        if (bytes_read == 0) {
            break; // end of file
        }
        total_bytes_read += bytes_read;
        if (buffer[total_bytes_read - 1] == '\n') {
            break; // end of line
        }
    }
    buffer[total_bytes_read] = '\0'; 
    return total_bytes_read; 
}

int create_file_with_size(char* filename, int filesize) {
    int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
    if (fd == -1) {
        perror("Error creating file");
        return -1;
    }
    // Write zeros to the file to initialize it
    for (int i = 0; i < filesize; i++) {
        write(fd, " ", 1);
    }
    return fd;
}

int get_offset_position_file_for_test(char* filename, int num) {
    // add position in file for test
    char buffer[1024];
    int fd = open(filename, O_RDONLY);
    int offset = 0;
    if (num!=1){
        offset = read_line(fd, buffer,1024) -1;
    } 
    close(fd);
    return offset;
}

int write_position_in_file_for_test(int fd, int position, int data) {
    char position_char[32];
    lseek(fd, position, SEEK_SET);
    if (position != 0) {
        snprintf(position_char, sizeof(position_char), ",%d", data);
    } else {
        snprintf(position_char, sizeof(position_char), "%d", data);
    }
    write(fd, position_char, strlen(position_char));
    write(fd, "\n", 1);
    return position;
}

int create_data_in_position(int fd, int position, int num) {
    // add "Hello World" in the position 
    lseek(fd, position, SEEK_SET);
    write(fd, WRITE_DATA, strlen(WRITE_DATA));
    return position;
}

int read_file(char* filename, int position) {
    int fd = open(filename, O_RDONLY);
    if (fd == -1) {
        perror("Error opening file");
        return -1;
    }
    lseek(fd, position, SEEK_SET);
    char buffer[strlen(WRITE_DATA) + 1];
    read(fd, buffer, strlen(WRITE_DATA) + 1);
    close(fd);
    return strlen(buffer);
}

int main(int argc, char** argv) {

    srand(time(NULL));
    int fd, log = -1;
    int position, readed, filesize_max;
    clock_t start_time, end_time;
    double create_time, write_time, read_time;

    //Create a csv file to store time only if the name is in arguments
    if (argc == 2) {
        log = open(argv[1], O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
        if (log == -1) {
            perror("Error creating file");
            return 2;
        }
        dprintf(log, "\"filesize\",\"position\",\"create_time\",\"write_time\",\"read_time\"\n");
    }

    for (int i = 1; i <= (int)(FILESIZE_MAX - 1) / FILESIZE_PACE; i++) {
        char filename[28];
        snprintf(filename, sizeof(filename), "%s/test/file_%d", FOLDER, i);
        filesize_max = i * FILESIZE_PACE;

        // Create a new file with variable size
        start_time = clock();
        fd = create_file_with_size(filename, filesize_max);
        end_time = clock();
        create_time = ((double)(end_time - start_time)) / CLOCKS_PER_SEC;
        close(fd);

        for (int p = 1; p <= POSITION_PACE; p++) {
            position = (int)((float)p / POSITION_PACE * (filesize_max));

            // add data in the position of the file for the test
            int offset = get_offset_position_file_for_test(filename, p);
            fd = open(filename, O_WRONLY);
            write_position_in_file_for_test(fd, offset, position);
            close(fd);

            // add data in the position
            fd = open(filename, O_WRONLY);
            start_time = clock();
            create_data_in_position(fd, position, p);
            end_time = clock();
            write_time = ((double)(end_time - start_time)) / CLOCKS_PER_SEC;
            close(fd);

            // read entirely the file
            start_time = clock();
            readed = read_file(filename, position);
            end_time = clock();
            read_time = ((double)(end_time - start_time)) / CLOCKS_PER_SEC;

            printf("taille fichier : %d, position : %d, create : %fs, ecriture: %fs, lecture: %fs\n",
                filesize_max, position, create_time, write_time, read_time);

            //update the csv file to store time
            if (argc == 2) {
                dprintf(log, "%d,%d,%f,%f,%f\n",
                    filesize_max, position, create_time, write_time, read_time);
            }
        }
    }

    if (log != -1) {
        close(log);
    }

    return 0;
}
