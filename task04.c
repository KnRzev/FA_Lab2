#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    STATUS_OK,
    STATUS_ARGUMENT,
    STATUS_FORMAT,
    STATUS_OVERFLOW,
    STATUS_MEMORY,
    STATUS_IO
} Status;

typedef struct {
    char *name;
    size_t size;
    uint64_t importance;
} File;

typedef struct {
    File *files;
    size_t count;
    size_t total_size;
    uint64_t total_importance;
} Backup;

static Status read_fields(FILE *stream, size_t wanted, char **line_out, char **fields) {
    if (stream == NULL || wanted == 0 || line_out == NULL || fields == NULL) {
        return STATUS_ARGUMENT;
    }
    *line_out = NULL;
    size_t length = 0;
    size_t capacity = 128U;
    char *line = malloc(capacity);
    if (line == NULL) {
        return STATUS_MEMORY;
    }
    Status status = STATUS_OK;
    int c = fgetc(stream);
    while (c != EOF && c != '\n') {
        if (c == 0) {
            status = STATUS_FORMAT;
            break;
        }
        if (length == capacity - 1U) {
            if (capacity > SIZE_MAX / 2U) {
                status = STATUS_OVERFLOW;
                break;
            }
            char *grown = realloc(line, capacity * 2U);
            if (grown == NULL) {
                status = STATUS_MEMORY;
                break;
            }
            line = grown;
            capacity *= 2U;
        }
        line[length] = (char)c;
        ++length;
        c = fgetc(stream);
    }
    if (ferror(stream)) {
        status = STATUS_IO;
    }
    line[length] = '\0';
    if (status == STATUS_OK) {
        char *word = strtok(line, " \t\r\v\f");
        for (size_t i = 0; i < wanted; ++i) {
            if (word == NULL) {
                status = STATUS_FORMAT;
                break;
            }
            fields[i] = word;
            word = strtok(NULL, " \t\r\v\f");
        }
        if (word != NULL) {
            status = STATUS_FORMAT;
        }
    }
    if (status != STATUS_OK) {
        free(line);
    } else {
        *line_out = line;
    }
    return status;
}

static Status parse_unsigned(const char *text, uint64_t *result) {
    if (text == NULL || text[0] == '\0' || result == NULL) {
        return STATUS_ARGUMENT;
    }
    uint64_t value = 0;
    for (size_t i = 0; text[i] != '\0'; ++i) {
        if (text[i] < '0' || text[i] > '9') {
            return STATUS_FORMAT;
        }
        unsigned int digit = (unsigned int)(text[i] - '0');
        if (value > (UINT64_MAX - digit) / 10U) {
            return STATUS_OVERFLOW;
        }
        value = value * 10U + digit;
    }
    *result = value;
    return STATUS_OK;
}

static Status validate_files(const File *files, size_t count) {
    if (files == NULL && count != 0) {
        return STATUS_ARGUMENT;
    }
    for (size_t i = 0; i < count; ++i) {
        if (files[i].name == NULL || files[i].name[0] == '\0' || files[i].size == 0 ||
            strpbrk(files[i].name, " \t\n\r\v\f") != NULL) {
            return STATUS_ARGUMENT;
        }
        for (size_t j = 0; j < i; ++j) {
            if (strcmp(files[i].name, files[j].name) == 0) {
                return STATUS_ARGUMENT;
            }
        }
    }
    return STATUS_OK;
}

static Status read_files(FILE *stream, File **result, size_t *count, size_t *capacity) {
    if (stream == NULL || result == NULL || count == NULL || capacity == NULL) {
        return STATUS_ARGUMENT;
    }
    *result = NULL;
    *count = 0;
    char *line = NULL;
    char *fields[3];
    uint64_t n = 0;
    uint64_t limit = 0;
    Status status = read_fields(stream, 2U, &line, fields);
    if (status == STATUS_OK) {
        status = parse_unsigned(fields[0], &n);
    }
    if (status == STATUS_OK) {
        status = parse_unsigned(fields[1], &limit);
    }
    free(line);
    if (status != STATUS_OK) {
        return status;
    }
    if (n == 0 || n > SIZE_MAX / sizeof(File) || limit > SIZE_MAX) {
        return STATUS_ARGUMENT;
    }
    *capacity = (size_t)limit;
    *result = calloc((size_t)n, sizeof(File));
    if (*result == NULL) {
        return STATUS_MEMORY;
    }
    for (size_t i = 0; i < (size_t)n; ++i) {
        line = NULL;
        uint64_t size = 0;
        uint64_t importance = 0;
        status = read_fields(stream, 3U, &line, fields);
        if (status == STATUS_OK) {
            status = parse_unsigned(fields[1], &size);
        }
        if (status == STATUS_OK) {
            status = parse_unsigned(fields[2], &importance);
        }
        if (status == STATUS_OK && (size == 0 || size > SIZE_MAX)) {
            status = STATUS_ARGUMENT;
        }
        if (status != STATUS_OK) {
            free(line);
            return status;
        }
        memmove(line, fields[0], strlen(fields[0]) + 1U);
        (*result)[i].name = line;
        (*result)[i].size = (size_t)size;
        (*result)[i].importance = importance;
        *count += 1U;
    }
    int c = fgetc(stream);
    while (c != EOF) {
        if (!isspace((unsigned char)c)) {
            return STATUS_FORMAT;
        }
        c = fgetc(stream);
    }
    if (ferror(stream)) {
        return STATUS_IO;
    }
    return validate_files(*result, *count);
}

static Status select_backup(const File *files, size_t count, size_t capacity, Backup *result) {
    if (result == NULL) {
        return STATUS_ARGUMENT;
    }
    *result = (Backup){NULL, 0, 0, 0};
    if (count > SIZE_MAX / sizeof(File)) {
        return STATUS_OVERFLOW;
    }
    Status status = validate_files(files, count);
    if (status != STATUS_OK) {
        return status;
    }
    if (count == 0 || capacity == 0) {
        return STATUS_OK;
    }
    size_t suitable = 0;
    for (size_t i = 0; i < count; ++i) {
        if (files[i].size <= capacity && files[i].importance != 0) {
            ++suitable;
        }
    }
    if (suitable == 0) {
        return STATUS_OK;
    }
    result->files = malloc(count * sizeof(File));
    if (result->files == NULL) {
        return STATUS_MEMORY;
    }
    size_t remaining = capacity;
    uint64_t total = 0;
    size_t fitting = 0;
    while (fitting < count && files[fitting].size <= remaining) {
        if (files[fitting].importance > UINT64_MAX - total) {
            return STATUS_OVERFLOW;
        }
        remaining -= files[fitting].size;
        total += files[fitting].importance;
        ++fitting;
    }
    if (fitting == count) {
        memcpy(result->files, files, count * sizeof(File));
        result->count = count;
        result->total_size = capacity - remaining;
        result->total_importance = total;
        return STATUS_OK;
    }
    if (capacity == SIZE_MAX || count + 1U > SIZE_MAX / (capacity + 1U) / sizeof(uint64_t)) {
        return STATUS_OVERFLOW;
    }
    size_t columns = capacity + 1U;
    uint64_t *table = calloc((count + 1U) * columns, sizeof(uint64_t));
    if (table == NULL) {
        return STATUS_MEMORY;
    }
    for (size_t i = count; i > 0 && status == STATUS_OK; --i) {
        const File *file = &files[i - 1U];
        for (size_t space = 0; space < columns; ++space) {
            uint64_t best = table[i * columns + space];
            if (file->size <= space) {
                uint64_t previous = table[i * columns + space - file->size];
                if (file->importance > UINT64_MAX - previous) {
                    status = STATUS_OVERFLOW;
                    break;
                }
                uint64_t included = previous + file->importance;
                if (included > best) {
                    best = included;
                }
            }
            table[(i - 1U) * columns + space] = best;
        }
    }
    if (status == STATUS_OK) {
        remaining = capacity;
        for (size_t i = 0; i < count; ++i) {
            if (table[i * columns + remaining] != table[(i + 1U) * columns + remaining]) {
                result->files[result->count] = files[i];
                result->count += 1U;
                remaining -= files[i].size;
            }
        }
        result->total_size = capacity - remaining;
        result->total_importance = table[capacity];
    }
    free(table);
    return status;
}

static Status print_backup(FILE *stream, const Backup *backup) {
    if (stream == NULL || backup == NULL || (backup->files == NULL && backup->count != 0)) {
        return STATUS_ARGUMENT;
    }
    if (fprintf(stream, "TOTAL_SIZE %zu\nTOTAL_IMPORTANCE %llu\nFILES %zu\n", backup->total_size,
                (unsigned long long)backup->total_importance, backup->count) < 0) {
        return STATUS_IO;
    }
    for (size_t i = 0; i < backup->count; ++i) {
        if (backup->files[i].name == NULL || backup->files[i].name[0] == '\0') {
            return STATUS_ARGUMENT;
        }
        if (fprintf(stream, "%s\n", backup->files[i].name) < 0) {
            return STATUS_IO;
        }
    }
    return STATUS_OK;
}

int main(int argc, char *argv[]) {
    if (argc != 1) {
        fprintf(stderr, "Использование: %s < input.txt\n", argv[0]);
        return EXIT_FAILURE;
    }
    File *files = NULL;
    size_t count = 0;
    size_t capacity = 0;
    Backup backup = {NULL, 0, 0, 0};
    Status status = read_files(stdin, &files, &count, &capacity);
    if (status == STATUS_OK) {
        status = select_backup(files, count, capacity, &backup);
    }
    if (status == STATUS_OK) {
        status = print_backup(stdout, &backup);
    }
    if (status == STATUS_OK) {
        if (fflush(stdout) == EOF) {
            status = STATUS_IO;
        }
    }
    free(backup.files);
    for (size_t i = 0; i < count; ++i) {
        free(files[i].name);
    }
    free(files);
    if (status != STATUS_OK) {
        fprintf(stderr, "Ошибка (код %d).\n", (int)status);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
