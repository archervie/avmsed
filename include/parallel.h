#ifndef PARALLEL_H
#define PARALLEL_H

#include "common.h"

void add_to_output(chunk *chunk, char *string_output, size_t string_length);
void *substitution_thread(void *arg);
void *translation_thread(void *arg);
void parallel_substitution(size_t chunk_size, int number_of_threads,
                           char *initial, char *final, char *file);
void parallel_translation(size_t chunk_size, int number_of_threads,
                          char *initial, char *final, char *file);

#endif
