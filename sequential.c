#include "sequential.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void sequential_substitution(const char *initial, const char *final,
                             const char *file) {
    FILE *fp = fopen(file, "r");
    if (!fp) {
        fprintf(stderr, "avmsed: cannot open file\n");
        exit(EXIT_FAILURE);
    }

    char *line = NULL;
    size_t size = 0;
    ssize_t read;
    size_t init_len = strlen(initial);

    while ((read = getline(&line, &size, fp)) != -1) {
        char *current = line;
        char *match;

        while ((match = strstr(current, initial)) != NULL) {
            while (current < match) {
                putchar(*current++);
            }
            fputs(final, stdout);
            current = match + init_len;
        }
        fputs(current, stdout);
    }

    free(line);
    fclose(fp);
}

void sequential_translation(const char *initial, const char *final,
                            const char *file) {
    size_t init_len = strlen(initial);
    if (init_len != strlen(final)) {
        fprintf(stderr, "avmsed: Translation strings not equal length\n");
        exit(EXIT_FAILURE);
    }

    FILE *fp = fopen(file, "r");
    if (!fp) {
        fprintf(stderr, "avmsed: cannot open file\n");
        exit(EXIT_FAILURE);
    }

    char *line = NULL;
    size_t size = 0;
    ssize_t read;

    while ((read = getline(&line, &size, fp)) != -1) {
        for (ssize_t i = 0; i < read; i++) {
            int found = 0;
            for (size_t j = 0; j < init_len; j++) {
                if (line[i] == initial[j]) {
                    putchar(final[j]);
                    found = 1;
                    break;
                }
            }
            if (!found) {
                putchar(line[i]);
            }
        }
    }

    free(line);
    fclose(fp);
}
