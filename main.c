#include "common.h"
#include "parallel.h"
#include "sequential.h"
#include <getopt.h>
#include <string.h>
#include <sys/sysinfo.h>

int main(int argc, char *argv[]) {
    int c;
    int option_index = 0;
    int number_of_threads = 1;
    size_t chunk_size = 0;
    int mode = -1;
    int threading = 0;

    while (1) {
        static struct option long_options[] = {
            {"help", no_argument, 0, 'h'},
            {"threading", required_argument, 0, 's'},
            {"mode", required_argument, 0, 'm'},
            {0, 0, 0, 0}};

        c = getopt_long(argc, argv, "m:hs:", long_options, &option_index);

        if (c == -1)
            break;

        switch (c) {
        case 'm':
            if (mode != -1) {
                option_error();
            }

            else if (strcmp(optarg, "substitution") == 0) {
                mode = 0;

            } else if (strcmp(optarg, "translation") == 0) {
                mode = 1;

            } else {
                unknown_mode_error();
            }

            break;

        case 's':
            chunk_size = atol(optarg);
            number_of_threads = get_nprocs();
            threading = 1;

            if (chunk_size < (size_t)number_of_threads) {
                number_of_threads = chunk_size;
            }

            break;

        case 'h':
            printf("Usage: avmsed [-m mode] [initial] [final] [file]\n");
            printf("-m  mode\n");
            printf("	\"substitution\" or \"translation\". Default: "
                   "substitution.\n");
            printf("initial & final\n");
            printf("	 in substitution, all occurrences of initial in [file] "
                   "are replaced with final\n");
            printf(
                "	 in translation, each character in initial is replaced "
                "with the the corresponding character from final in [file]\n");
            exit(EXIT_SUCCESS);

        default:
            option_error();
        }
    }

    if (argc - optind != 3) {
        option_error();
    }

    char *initial = argv[optind];
    char *final = argv[optind + 1];
    char *file = argv[optind + 2];

    if (mode == -1) {
        mode = 0;
    }

    switch (mode) {

    case 0:
        if (threading) {
            parallel_substitution(chunk_size, number_of_threads, initial, final,
                                  file);
            break;
        } else {
            sequential_substitution(initial, final, file);
            break;
        }

    case 1:
        if (threading) {
            parallel_translation(chunk_size, number_of_threads, initial, final,
                                 file);
            break;
        } else {
            sequential_translation(initial, final, file);
            break;
        }

    default:
        option_error();
    }
    exit(EXIT_SUCCESS);
}
