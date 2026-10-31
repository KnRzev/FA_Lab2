#include <ctype.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef enum {
    STATUS_OK,
    STATUS_ARGUMENT,
    STATUS_NUMBER,
    STATUS_OVERFLOW,
    STATUS_MEMORY,
    STATUS_IO
} Status;

static Status string_length(const char *text, size_t *result) {
    if (text == NULL || result == NULL) {
        return STATUS_ARGUMENT;
    }
    size_t length = 0;

    while (text[length] != '\0') {
        if (length == SIZE_MAX - 1U) {
            return STATUS_OVERFLOW;
        }
        ++length;
    }

    *result = length;
    return STATUS_OK;
}

static Status parse_seed(const char *text, unsigned int *result) {
    if (text == NULL || result == NULL || text[0] == '\0') {
        return STATUS_ARGUMENT;
    }

    unsigned int value = 0;

    for (size_t i = 0; text[i] != '\0'; ++i) {
        if (text[i] < '0' || text[i] > '9') {
            return STATUS_NUMBER;
        }

        unsigned int digit = (unsigned int)(text[i] - '0');
        if (value > (UINT_MAX - digit) / 10U) {
            return STATUS_OVERFLOW;
        }
        value = value * 10U + digit;
    }
    *result = value;
    return STATUS_OK;
}

static Status reverse_string(const char *text, char **result) {
    if (result == NULL) {
        return STATUS_ARGUMENT;
    }

    *result = NULL;
    size_t length;
    Status status = string_length(text, &length);
    if (status != STATUS_OK) {
        return status;
    }

    char *copy = malloc(length + 1U);
    if (copy == NULL) {
        return STATUS_MEMORY;
    }

    for (size_t i = 0; i < length; ++i) {
        copy[i] = text[length - 1U - i];
    }

    copy[length] = '\0';
    *result = copy;
    return STATUS_OK;
}

static Status uppercase_odd(const char *text, char **result) {
    if (result == NULL) {
        return STATUS_ARGUMENT;
    }

    *result = NULL;
    size_t length;
    Status status = string_length(text, &length);
    if (status != STATUS_OK) {
        return status;
    }

    char *copy = malloc(length + 1U);
    if (copy == NULL) {
        return STATUS_MEMORY;
    }

    for (size_t i = 0; i < length; ++i) {
        if (i % 2U == 1U) {
            copy[i] = (char)toupper((unsigned char)text[i]);
        } else {
            copy[i] = text[i];
        }
    }

    copy[length] = '\0';
    *result = copy;
    return STATUS_OK;
}

static Status group_characters(const char *text, char **result) {
    if (result == NULL) {
        return STATUS_ARGUMENT;
    }

    *result = NULL;
    size_t length;
    Status status = string_length(text, &length);
    if (status != STATUS_OK) {
        return status;
    }

    char *copy = malloc(length + 1U);
    if (copy == NULL) {
        return STATUS_MEMORY;
    }

    size_t position = 0;

    for (int group = 0; group < 3; ++group) {
        for (size_t i = 0; i < length; ++i) {
            unsigned char c = (unsigned char)text[i];
            int category;

            if (isdigit(c)) {
                category = 0;
            } else if (isalpha(c)) {
                category = 1;
            } else {
                category = 2;
            }
            if (category == group) {
                copy[position] = text[i];
                ++position;
            }
        }
    }

    copy[position] = '\0';
    *result = copy;
    return STATUS_OK;
}

static Status concatenate_random(const char *const *strings, size_t count,
                                 unsigned int seed, char **result) {
    if (strings == NULL || count == 0 || result == NULL) {
        return STATUS_ARGUMENT;
    }

    *result = NULL;
    if (count > SIZE_MAX / sizeof(size_t)) {
        return STATUS_OVERFLOW;
    }

    size_t total = 0;

    for (size_t i = 0; i < count; ++i) {
        size_t length;
        Status status = string_length(strings[i], &length);
        if (status != STATUS_OK) {
            return status;
        }

        if (length > SIZE_MAX - 1U - total) {
            return STATUS_OVERFLOW;
        }
        total += length;
    }

    size_t *order = malloc(count * sizeof(*order));
    char *joined = malloc(total + 1U);
    if (order == NULL || joined == NULL) {
        free(order);
        free(joined);
        return STATUS_MEMORY;
    }

    for (size_t i = 0; i < count; ++i) {
        order[i] = i;
    }

    srand(seed);
    for (size_t i = count; i > 1U; --i) {
        size_t index = (size_t)rand() % i;
        size_t temporary = order[i - 1U];
        order[i - 1U] = order[index];
        order[index] = temporary;
    }

    size_t position = 0;

    for (size_t i = 0; i < count; ++i) {
        const char *text = strings[order[i]];

        for (size_t j = 0; text[j] != '\0'; ++j) {
            joined[position] = text[j];
            ++position;
        }
    }

    joined[position] = '\0';
    free(order);
    *result = joined;
    return STATUS_OK;
}

int main(int argc, char *argv[]) {
    if (argc < 3 || argv[1][0] != '-' || argv[1][1] == '\0' || argv[1][2] != '\0') {
        fprintf(stderr, "Использование: %s <-l|-r|-u|-n> <строка>\n       %s -c <строка> <seed> [строки...]\n", argv[0], argv[0]);
        return EXIT_FAILURE;
    }
    char flag = argv[1][1];
    if ((flag == 'c' && argc < 4) || (flag != 'c' && argc != 3)) {
        fputs("Ошибка: неверное количество аргументов.\n", stderr);
        return EXIT_FAILURE;
    }

    Status status = STATUS_OK;
    char *result = NULL;
    if (flag == 'l') {
        size_t length;
        status = string_length(argv[2], &length);
        if (status == STATUS_OK) {
            if (printf("%zu\n", length) < 0) {
                status = STATUS_IO;
            }
        }
    } else if (flag == 'r') {
        status = reverse_string(argv[2], &result);
    } else if (flag == 'u') {
        status = uppercase_odd(argv[2], &result);
    } else if (flag == 'n') {
        status = group_characters(argv[2], &result);
    } else if (flag == 'c') {
        unsigned int seed;
        status = parse_seed(argv[3], &seed);
        size_t count = (size_t)argc - 3U;
        const char **strings = NULL;
        if (status == STATUS_OK) {
            if (count > SIZE_MAX / sizeof(*strings)) {
                status = STATUS_OVERFLOW;
            } else {
                strings = malloc(count * sizeof(*strings));
                if (strings == NULL) {
                    status = STATUS_MEMORY;
                }
            }
        }

        if (status == STATUS_OK) {
            strings[0] = argv[2];
            for (size_t i = 1; i < count; ++i) {
                strings[i] = argv[i + 3U];
            }
            status = concatenate_random(strings, count, seed, &result);
        }
        free(strings);
    } else {
        status = STATUS_ARGUMENT;
    }

    if (status == STATUS_OK && result != NULL) {
        if (puts(result) == EOF) {
            status = STATUS_IO;
        }
    }
    free(result);
    if (status == STATUS_OK) {
        if (fflush(stdout) == EOF) {
            status = STATUS_IO;
        }
    }

    if (status != STATUS_OK) {
        fprintf(stderr, "Ошибка (код %d).\n", (int)status);
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
