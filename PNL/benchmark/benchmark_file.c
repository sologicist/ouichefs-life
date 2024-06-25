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

FILE* create_file_with_size(char* filename, int filesize) {
    FILE* file = fopen(filename, "w");
    if (file == NULL) {
        perror("Error creating file");
        return NULL;
    }
    // Write zeros to the file to initialize it
    for (int i = 0; i < filesize; i++) {
        fputc(' ', file);
    }
    return file;
}

int get_offset_position_file_for_test(char* filename, int num) {
    // add position in file for test
    int offset = 0;
    FILE* fr = fopen(filename,  "r");
    fseek(fr, 0, SEEK_SET);
    char buffer[1024];

    // Place the pointer at the end of the 1st line
    if ( num!=1 && fgets(buffer, sizeof(buffer), fr) != NULL) {
        fseek(fr, -1, SEEK_CUR);
        if (buffer[strlen(buffer) - 1] == '\n') {
            fseek(fr, -1, SEEK_CUR);
        }
        offset = ftell(fr)+1;
    }
    fclose(fr);
    return offset;
}

int write_position_in_file_for_test(FILE* file, int position, int data){
    char position_char[32];
    fseek(file, sizeof(char)*position, SEEK_SET);
    if (position != 0) {
        snprintf(position_char, sizeof(position_char), ",%d", data);
    } else {
        snprintf(position_char, sizeof(position_char), "%d", data);
    }
    fprintf(file, "%s\n", position_char);
    return position;
}

int create_data_in_position(FILE* file, int position, int num) {
    // add "Hello World" in the position 
    fseek(file, position, SEEK_SET);
    fprintf(file, WRITE_DATA);
    return position;
}

int read_file(char* filename, int position) {
    FILE* file = fopen(filename,  "r");
    int len = strlen(WRITE_DATA)+1; 
    char buffer[len];
    
    fseek(file, position, SEEK_SET);
    fgets(buffer, len, file);
    
    fclose(file);
    return len;
}

int main(int argc, char** argv) {

    srand(time(NULL));
    FILE* file,* log=NULL;
    int position, readed, filesize_max;
    clock_t start_time, end_time;
    double create_time, write_time, read_time;

    //Create a csv file to store time only if the name is in arguments
    if(argc==2){
        log = fopen(argv[1], "w");
        if(log==NULL){
            perror("Error creating file");
            return 2;
        }
        fprintf(log, "\"filesize\",\"position\",\"create_time\",\"write_time\",\"read_time\"\n");
    }

    for (int i=1;i<=(int) (FILESIZE_MAX-1)/FILESIZE_PACE;i++){
        char filename[28];
        snprintf(filename, sizeof(filename), "%s/test/file_%d", FOLDER, i);
        filesize_max = i*FILESIZE_PACE;
        
        // Create a new file with variable size
        start_time = clock();
        file = create_file_with_size(filename, filesize_max);
        end_time = clock();
        create_time = ((double)(end_time - start_time)) / CLOCKS_PER_SEC;

        for(int p=1;p<=POSITION_PACE;p++){
            position = (int) ((float) p/POSITION_PACE*(filesize_max));

            // add data in the position of the file for the test
            int offset = get_offset_position_file_for_test(filename, p);
            write_position_in_file_for_test(file, offset, position);

            // add data un the position
            start_time = clock();
            create_data_in_position(file, position, p);
            end_time = clock();
            write_time = ((double)(end_time - start_time)) / CLOCKS_PER_SEC;

            // read entirely the file
            start_time = clock();
            readed = read_file(filename, position);
            end_time = clock();
            read_time = ((double)(end_time - start_time)) / CLOCKS_PER_SEC;

            printf("taille fichier : %d, position : %d, create : %fs, ecriture: %fs, lecture: %fs\n",
                filesize_max, position, create_time, write_time, read_time);

            //update the csv file to store time
            if(argc==2){
                fprintf(log, "%d,%d,%f,%f,%f\n", 
                    filesize_max, position, create_time, write_time, read_time);
            }
        }
    }

    if(argc==2){
        fclose(log);
    }

    return 0;
}