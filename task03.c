#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    STATUS_OK,
    STATUS_ARGUMENT,
    STATUS_MEMORY,
    STATUS_OVERFLOW,
    STATUS_IO
} Status;

typedef struct {
    size_t line;
    size_t column;
} Match;

typedef struct {
    const char *path;
    Match *matches;
    size_t count;
} FileMatches;

static Status read_file(const char *path, unsigned char **result, size_t *length) {
    if (path == NULL || path[0] == '\0' || result == NULL || length == NULL) {
        return STATUS_ARGUMENT;
    }
    *result = NULL;
    *length = 0;
    FILE *stream = fopen(path, "rb");
    if (stream == NULL) {
        return STATUS_IO;
    }
    unsigned char *data = NULL;
    size_t size = 0;
    size_t capacity = 0;
    Status status = STATUS_OK;
    int c = fgetc(stream);
    while (c != EOF) {
        if (size == capacity) {
            if (capacity > SIZE_MAX / 2U) {
                status = STATUS_OVERFLOW;
                break;
            }
            size_t next = 4096U;
            if (capacity != 0) {
                next = capacity * 2U;
            }
            unsigned char *grown = realloc(data, next);
            if (grown == NULL) {
                status = STATUS_MEMORY;
                break;
            }
            data = grown;
            capacity = next;
        }
        data[size] = (unsigned char)c;
        ++size;
        c = fgetc(stream);
    }
    if (ferror(stream)) {
        status = STATUS_IO;
    }
    int close_result = fclose(stream);
    if (close_result != 0 && status == STATUS_OK) {
        status = STATUS_IO;
    }
    if (status != STATUS_OK) {
        free(data);
    } else {
        *result = data;
        *length = size;
    }
    return status;
}

static Status find_matches(const unsigned char *data, size_t length, const char *pattern,
                           Match **result, size_t *count) {
    if (pattern == NULL || pattern[0] == '\0' || result == NULL || count == NULL) {
        return STATUS_ARGUMENT;
    }
    *result = NULL;
    *count = 0;
    if (length == 0) {
        return STATUS_OK;
    }
    if (data == NULL) {
        return STATUS_ARGUMENT;
    }
    size_t pattern_length = strlen(pattern);
    if (pattern_length > length) {
        return STATUS_OK;
    }
    size_t last = length - pattern_length;
    size_t capacity = 0;
    size_t line = 1;
    size_t column = 1;
    for (size_t i = 0; i <= last; ++i) {
        size_t j = 0;
        while (j < pattern_length && data[i + j] == (unsigned char)pattern[j]) {
            ++j;
        }
        if (j == pattern_length) {
            if (*count == capacity) {
                if (capacity > SIZE_MAX / sizeof(Match) / 2U) {
                    return STATUS_OVERFLOW;
                }
                size_t next = 16U;
                if (capacity != 0) {
                    next = capacity * 2U;
                }
                Match *grown = realloc(*result, next * sizeof(Match));
                if (grown == NULL) {
                    return STATUS_MEMORY;
                }
                *result = grown;
                capacity = next;
            }
            (*result)[*count].line = line;
            (*result)[*count].column = column;
            *count += 1U;
        }
        if (i < last) {
            if (data[i] == '\n') {
                ++line;
                column = 1;
            } else {
                ++column;
            }
        }
    }
    return STATUS_OK;
}

static Status search_files(const char *pattern, size_t file_count, FileMatches **result, ...) {
    if (pattern == NULL || pattern[0] == '\0' || file_count == 0 || result == NULL) {
        return STATUS_ARGUMENT;
    }
    *result = NULL;
    if (file_count > SIZE_MAX / sizeof(FileMatches)) {
        return STATUS_OVERFLOW;
    }
    FileMatches *reports = calloc(file_count, sizeof(*reports));
    if (reports == NULL) {
        return STATUS_MEMORY;
    }
    va_list args;
    va_start(args, result);
    Status status = STATUS_OK;
    for (size_t i = 0; i < file_count && status == STATUS_OK; ++i) {
        reports[i].path = va_arg(args, const char *);
        unsigned char *data = NULL;
        size_t length = 0;
        status = read_file(reports[i].path, &data, &length);
        if (status == STATUS_OK) {
            status = find_matches(data, length, pattern, &reports[i].matches, &reports[i].count);
        }
        free(data);
    }
    va_end(args);
    if (status != STATUS_OK) {
        for (size_t i = 0; i < file_count; ++i) {
            free(reports[i].matches);
        }
        free(reports);
    } else {
        *result = reports;
    }
    return status;
}

static Status print_reports(const FileMatches *reports, size_t count) {
    if (reports == NULL || count == 0) {
        return STATUS_ARGUMENT;
    }
    for (size_t i = 0; i < count; ++i) {
        if (reports[i].path == NULL || (reports[i].count != 0 && reports[i].matches == NULL)) {
            return STATUS_ARGUMENT;
        }
        if (printf("%s: %zu\n", reports[i].path, reports[i].count) < 0) {
            return STATUS_IO;
        }
        for (size_t j = 0; j < reports[i].count; ++j) {
            if (printf("%zu %zu\n", reports[i].matches[j].line, reports[i].matches[j].column) < 0) {
                return STATUS_IO;
            }
        }
    }
    return STATUS_OK;
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Использование: %s <подстрока> <файл> [файлы...]\n", argv[0]);
        return EXIT_FAILURE;
    }
    Status status = STATUS_OK;
    int index = 2;
    while (index < argc && status == STATUS_OK) {
        size_t count = 1U;
        if (argc - index >= 2) {
            count = 2U;
        }
        FileMatches *reports = NULL;
        if (count == 2U) {
            status = search_files(argv[1], count, &reports,
                                  (const char *)argv[index], (const char *)argv[index + 1]);
        } else {
            status = search_files(argv[1], count, &reports, (const char *)argv[index]);
        }
        if (status == STATUS_OK) {
            status = print_reports(reports, count);
        }
        if (reports != NULL) {
            for (size_t i = 0; i < count; ++i) {
                free(reports[i].matches);
            }
            free(reports);
        }
        index += (int)count;
    }
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
