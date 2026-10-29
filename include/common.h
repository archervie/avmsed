#ifndef COMMON_H
#define COMMON_H

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    off_t file_offset;
    int index;
    size_t length;
    char *output;
    size_t output_length;
    size_t max_buffer_size;
    volatile int is_done;
} chunk;

typedef struct {
    char *initial;
    char *final;
    int fd;
} chunk_thread;

void open_file_error();
void option_error();
void unequal_length_error();
void unknown_mode_error();

#endif
