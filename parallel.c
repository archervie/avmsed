#include "parallel.h"
#include "common.h"

#include <fcntl.h>
#include <pthread.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

static chunk *chunk_queue = NULL;
static int number_of_chunks = 0;
static int next_chunk_index = 0;

static pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t print_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t print_cond = PTHREAD_COND_INITIALIZER;

void add_to_output(chunk *chunk, char *string_output, size_t string_length) {
    while (chunk->output_length + string_length > chunk->max_buffer_size) {
        if (chunk->max_buffer_size == 0) {
            chunk->max_buffer_size = chunk->length + 256;
            chunk->output = realloc(chunk->output, chunk->max_buffer_size);

            if (chunk->output == NULL) {
                option_error();
            }

        } else {
            chunk->max_buffer_size = chunk->max_buffer_size * 2;
            chunk->output = realloc(chunk->output, chunk->max_buffer_size);

            if (chunk->output == NULL) {
                option_error();
            }
        }
    }

    memcpy(chunk->output + chunk->output_length, string_output, string_length);
    chunk->output_length += string_length;
}

void *substitution_thread(void *arg) {
    chunk_thread *thread = (chunk_thread *)arg;

    while (1) {
        int target_index = -1;

        pthread_mutex_lock(&queue_mutex);

        if (next_chunk_index < number_of_chunks) {
            target_index = next_chunk_index;
            next_chunk_index++;
        }
        pthread_mutex_unlock(&queue_mutex);

        if (target_index == -1) {
            break;
        }

        chunk *chunk = &chunk_queue[target_index];
        chunk->output = NULL;
        chunk->output_length = 0;
        chunk->max_buffer_size = 0;

        if (chunk->length > 0) {
            long page_size = sysconf(_SC_PAGE_SIZE);
            off_t aligned_offset = (chunk->file_offset / page_size) * page_size;
            size_t diff = chunk->file_offset - aligned_offset;
            size_t map_size = chunk->length + diff;

            char *mapped = mmap(NULL, map_size, PROT_READ, MAP_PRIVATE,
                                thread->fd, aligned_offset);

            char *current = mapped + diff;
            char *chunk_end = current + chunk->length;
            char *match;

            size_t init_len = strlen(thread->initial);
            size_t final_len = strlen(thread->final);

            while ((match = memmem(current, chunk_end - current,
                                   thread->initial, init_len)) != NULL) {
                size_t leading_bytes = match - current;
                if (leading_bytes > 0) {
                    add_to_output(chunk, current, leading_bytes);
                }
                add_to_output(chunk, thread->final, final_len);
                current = match + init_len;
            }
            if (current < chunk_end) {
                add_to_output(chunk, current, chunk_end - current);
            }

            munmap(mapped, map_size);
        } else {
            chunk->output = NULL;
            chunk->output_length = 0;
        }
        pthread_mutex_lock(&print_mutex);
        chunk->is_done = 1;
        pthread_cond_signal(&print_cond);
        pthread_mutex_unlock(&print_mutex);
    }
    return NULL;
}

void *translation_thread(void *arg) {
    chunk_thread *thread = (chunk_thread *)arg;
    while (1) {
        int target_index = -1;
        pthread_mutex_lock(&queue_mutex);
        if (next_chunk_index < number_of_chunks) {
            target_index = next_chunk_index;
            next_chunk_index++;
        }
        pthread_mutex_unlock(&queue_mutex);

        if (target_index == -1) {
            break;
        }

        chunk *chunk = &chunk_queue[target_index];

        if (chunk->length > 0) {
            long page_size = sysconf(_SC_PAGE_SIZE);
            off_t aligned_offset = (chunk->file_offset / page_size) * page_size;
            size_t diff = chunk->file_offset - aligned_offset;
            size_t map_size = chunk->length + diff;

            char *mapped = mmap(NULL, map_size, PROT_READ, MAP_PRIVATE,
                                thread->fd, aligned_offset);
            if (mapped == MAP_FAILED) {
                option_error();
            }

            chunk->output = NULL;
            chunk->output_length = 0;
            chunk->max_buffer_size = 0;
            char *current = mapped + diff;

            chunk->output = malloc(chunk->length);
            memcpy(chunk->output, current, chunk->length);
            chunk->output_length = chunk->length;

            size_t init_len = strlen(thread->initial);

            for (size_t i = 0; i < chunk->length; i++) {
                for (size_t j = 0; j < init_len; j++) {
                    if (chunk->output[i] == thread->initial[j]) {
                        chunk->output[i] = thread->final[j];
                        break;
                    }
                }
            }

            munmap(mapped, map_size);
            pthread_mutex_lock(&print_mutex);
            chunk->is_done = 1;
            pthread_cond_signal(&print_cond);
            pthread_mutex_unlock(&print_mutex);
        } else {
            chunk->output = NULL;
            chunk->output_length = 0;
        }
    }
    return NULL;
}

void parallel_substitution(size_t chunk_size, int number_of_threads,
                           char *initial, char *final, char *file) {
    int fd = open(file, O_RDONLY, S_IRUSR | S_IWUSR);

    if (fd == -1) {
        open_file_error();
    }

    struct stat sb;

    if (fstat(fd, &sb) == -1) {
        option_error();
    }

    size_t total_byte_size = sb.st_size;

    if (!total_byte_size) {
        close(fd);
        exit(EXIT_SUCCESS);
    }

    // We create a queue here for the chunks; we
    // use a sliding pointer to determine the next
    // chunk index
    long page_size = sysconf(_SC_PAGE_SIZE);
    off_t current_offset = 0;
    int chunk_capacity = 64;
    chunk_queue = malloc(chunk_capacity * sizeof(chunk));
    number_of_chunks = 0;
    next_chunk_index = 0;

    while (current_offset < (off_t)total_byte_size) {
        off_t target_end = current_offset + chunk_size;

        if (target_end >= (off_t)total_byte_size) {
            target_end = total_byte_size;
        } else {
            // Integer division down to the nearest page size since
            // mmap needs a page size multiple
            off_t aligned_map_offset = (target_end / page_size) * page_size;
            size_t diff = target_end - aligned_map_offset;

            size_t scan_len = page_size * 2;
            if (aligned_map_offset + scan_len > total_byte_size) {
                scan_len = total_byte_size - aligned_map_offset;
            }

            char *map = mmap(NULL, scan_len, PROT_READ, MAP_PRIVATE, fd,
                             aligned_map_offset);

            if (map == MAP_FAILED) {
                option_error();
            }

            char *ptr = map + diff;
            char *map_end = map + scan_len;

            while (ptr < map_end && *ptr != '\n') {
                ptr++;
            }
            if (ptr < map_end && *ptr == '\n') {
                ptr++; // Include the newline in this chunk
            }

            target_end += (ptr - (map + diff));
            if (target_end > (off_t)total_byte_size) {
                target_end = total_byte_size;
            }

            munmap(map, scan_len);
        }

        // Dynamic queue resizing if needed
        if (number_of_chunks >= chunk_capacity) {
            chunk_capacity *= 2;
            chunk_queue = realloc(chunk_queue, chunk_capacity * sizeof(chunk));
        }

        chunk_queue[number_of_chunks].file_offset = current_offset;
        chunk_queue[number_of_chunks].length = target_end - current_offset;
        chunk_queue[number_of_chunks].index = number_of_chunks;
        chunk_queue[number_of_chunks].is_done = 0;
        chunk_queue[number_of_chunks].output = NULL;
        chunk_queue[number_of_chunks].output_length = 0;
        chunk_queue[number_of_chunks].max_buffer_size = 0;

        current_offset = target_end;
        number_of_chunks++;
    }

    chunk_thread thread = {.initial = initial, .final = final, .fd = fd};
    pthread_t *threads = malloc(number_of_threads * sizeof(pthread_t));

    for (int i = 0; i < number_of_threads; i++) {
        if (pthread_create(&threads[i], NULL, substitution_thread, &thread) !=
            0) {
            option_error();
        }
    }

    for (int i = 0; i < number_of_chunks; i++) {
        chunk *current_chunk = &chunk_queue[i];

        pthread_mutex_lock(&print_mutex);
        while (!current_chunk->is_done) {
            pthread_cond_wait(&print_cond, &print_mutex);
        }
        pthread_mutex_unlock(&print_mutex);

        fwrite(current_chunk->output, 1, current_chunk->output_length, stdout);

        free(current_chunk->output);
        current_chunk->output = NULL;
    }

    for (int i = 0; i < number_of_threads; i++) {
        pthread_join(threads[i], NULL);
    }

    free(threads);
    free(chunk_queue);
    close(fd);
}

void parallel_translation(size_t chunk_size, int number_of_threads,
                          char *initial, char *final, char *file) {
    if (strlen(initial) != strlen(final)) {
        unequal_length_error();
    }

    int fd = open(file, O_RDONLY, S_IRUSR | S_IWUSR);

    if (fd == -1) {
        open_file_error();
    }

    struct stat sb;

    if (fstat(fd, &sb) == -1) {
        option_error();
    }

    size_t total_byte_size = sb.st_size;

    if (!total_byte_size) {
        close(fd);
        exit(EXIT_SUCCESS);
    }

    long page_size = sysconf(_SC_PAGE_SIZE);
    off_t current_offset = 0;
    int chunk_capacity = 64;
    chunk_queue = malloc(chunk_capacity * sizeof(chunk));
    number_of_chunks = 0;
    next_chunk_index = 0;

    while (current_offset < (off_t)total_byte_size) {
        off_t target_end = current_offset + chunk_size;

        if (target_end >= (off_t)total_byte_size) {
            target_end = total_byte_size;
        } else {
            off_t aligned_map_offset = (target_end / page_size) * page_size;
            size_t diff = target_end - aligned_map_offset;

            size_t scan_len = page_size * 2;
            if (aligned_map_offset + scan_len > total_byte_size) {
                scan_len = total_byte_size - aligned_map_offset;
            }

            char *map = mmap(NULL, scan_len, PROT_READ, MAP_PRIVATE, fd,
                             aligned_map_offset);

            if (map == MAP_FAILED) {
                option_error();
            }

            char *ptr = map + diff;
            char *map_end = map + scan_len;

            while (ptr < map_end && *ptr != '\n') {
                ptr++;
            }
            if (ptr < map_end && *ptr == '\n') {
                ptr++;
            }

            target_end += (ptr - (map + diff));
            if (target_end > (off_t)total_byte_size) {
                target_end = total_byte_size;
            }

            munmap(map, scan_len);
        }

        if (number_of_chunks >= chunk_capacity) {
            chunk_capacity *= 2;
            chunk_queue = realloc(chunk_queue, chunk_capacity * sizeof(chunk));
        }

        chunk_queue[number_of_chunks].file_offset = current_offset;
        chunk_queue[number_of_chunks].length = target_end - current_offset;
        chunk_queue[number_of_chunks].index = number_of_chunks;
        chunk_queue[number_of_chunks].is_done = 0;
        chunk_queue[number_of_chunks].output = NULL;
        chunk_queue[number_of_chunks].output_length = 0;
        chunk_queue[number_of_chunks].max_buffer_size = 0;

        current_offset = target_end;
        number_of_chunks++;
    }

    chunk_thread thread = {.initial = initial, .final = final, .fd = fd};
    pthread_t *threads = malloc(number_of_threads * sizeof(pthread_t));

    for (int i = 0; i < number_of_threads; i++) {
        if (pthread_create(&threads[i], NULL, translation_thread, &thread) !=
            0) {
            option_error();
        }
    }

    for (int i = 0; i < number_of_chunks; i++) {
        chunk *current_chunk = &chunk_queue[i];

        pthread_mutex_lock(&print_mutex);
        while (!current_chunk->is_done) {
            pthread_cond_wait(&print_cond, &print_mutex);
        }
        pthread_mutex_unlock(&print_mutex);

        if (current_chunk->output_length > 0 && current_chunk->output != NULL) {
            fwrite(current_chunk->output, 1, current_chunk->output_length,
                   stdout);
        }

        free(current_chunk->output);
        current_chunk->output = NULL;
    }

    for (int i = 0; i < number_of_threads; i++) {
        pthread_join(threads[i], NULL);
    }

    free(threads);
    free(chunk_queue);
    close(fd);
}
