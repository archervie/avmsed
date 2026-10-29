#include "common.h"

void open_file_error() {
    fprintf(stderr, "avmsed: cannot open file\n");
    exit(EXIT_FAILURE);
}

void option_error() {
    fprintf(stderr, "avmsed: [option] [initial] [final] [file]\n");
    exit(EXIT_FAILURE);
}

void unequal_length_error() {
    fprintf(stderr, "avmsed: Translation strings not equal length\n");
    exit(EXIT_FAILURE);
}

void unknown_mode_error() {
    fprintf(stderr, "avmsed: unknown mode detected\n");
    fprintf(stderr, "mode must be \"substitution\" or \"translation\"\n");
    exit(EXIT_FAILURE);
}
