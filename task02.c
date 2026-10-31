#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    STATUS_OK,
    STATUS_ARGUMENT,
    STATUS_FORMAT,
    STATUS_RANGE,
    STATUS_MEMORY,
    STATUS_IO
} Status;

typedef struct {
    unsigned long long id;
    char *name;
    char *surname;
    long double salary;
    char *storage;
} Employee;

static Status read_line(FILE *stream, char **result, int *finished) {
    if (stream == NULL || result == NULL || finished == NULL) {
        return STATUS_ARGUMENT;
    }
    *result = NULL;
    *finished = 0;
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
                status = STATUS_RANGE;
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
    *finished = status == STATUS_OK && c == EOF && length == 0;
    if (status != STATUS_OK || *finished != 0) {
        free(line);
    } else {
        line[length] = '\0';
        *result = line;
    }
    return status;
}

static Status parse_id(const char *text, unsigned long long *result) {
    if (text == NULL || result == NULL || text[0] == '\0') {
        return STATUS_ARGUMENT;
    }
    unsigned long long value = 0;
    for (size_t i = 0; text[i] != '\0'; ++i) {
        if (text[i] < '0' || text[i] > '9') {
            return STATUS_FORMAT;
        }
        unsigned int digit = (unsigned int)(text[i] - '0');
        if (value > (ULLONG_MAX - digit) / 10ULL) {
            return STATUS_RANGE;
        }
        value = value * 10ULL + digit;
    }
    *result = value;
    return STATUS_OK;
}

static Status validate_employee(const Employee *employee) {
    if (employee == NULL || employee->name == NULL || employee->surname == NULL) {
        return STATUS_ARGUMENT;
    }
    const char letters[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    if (employee->name[0] == '\0' || employee->surname[0] == '\0' ||
        strspn(employee->name, letters) != strlen(employee->name) ||
        strspn(employee->surname, letters) != strlen(employee->surname) ||
        !isfinite(employee->salary) || employee->salary < 0.0L) {
        return STATUS_FORMAT;
    }
    return STATUS_OK;
}

static Status parse_employee(char *line, Employee *result) {
    if (line == NULL || result == NULL) {
        return STATUS_ARGUMENT;
    }
    char *fields[5];
    size_t count = 0;
    char *word = strtok(line, " \t\r\v\f");
    while (word != NULL && count < 5U) {
        fields[count] = word;
        ++count;
        word = strtok(NULL, " \t\r\v\f");
    }
    if (count != 4U) {
        return STATUS_FORMAT;
    }
    Employee employee = {0, fields[1], fields[2], 0.0L, line};
    Status status = parse_id(fields[0], &employee.id);
    char *end;
    employee.salary = strtold(fields[3], &end);
    if (end == fields[3] || *end != '\0' || fields[3][0] == '-' ||
        strspn(fields[3], "+-0123456789.eE") != strlen(fields[3])) {
        return STATUS_FORMAT;
    }
    size_t mantissa_length = strcspn(fields[3], "eE");
    if (!isfinite(employee.salary) || (fpclassify(employee.salary) == FP_ZERO &&
        strspn(fields[3], "+-.0") < mantissa_length)) {
        return STATUS_RANGE;
    }
    if (status == STATUS_OK) {
        status = validate_employee(&employee);
    }
    if (status == STATUS_OK) {
        *result = employee;
    }
    return status;
}

static Status read_employees(FILE *stream, Employee **result, size_t *count) {
    if (stream == NULL || result == NULL || count == NULL) {
        return STATUS_ARGUMENT;
    }
    *count = 0;
    size_t capacity = 16U;
    *result = malloc(capacity * sizeof(**result));
    if (*result == NULL) {
        return STATUS_MEMORY;
    }
    for (;;) {
        char *line = NULL;
        int finished;
        Status status = read_line(stream, &line, &finished);
        if (status != STATUS_OK || finished != 0) {
            return status;
        }
        Employee employee;
        status = parse_employee(line, &employee);
        if (status == STATUS_OK && *count == capacity) {
            if (capacity > SIZE_MAX / sizeof(**result) / 2U) {
                status = STATUS_RANGE;
            } else {
                Employee *grown = realloc(*result, capacity * 2U * sizeof(**result));
                if (grown == NULL) {
                    status = STATUS_MEMORY;
                } else {
                    *result = grown;
                    capacity *= 2U;
                }
            }
        }
        if (status != STATUS_OK) {
            free(line);
            return status;
        }
        (*result)[*count] = employee;
        *count += 1U;
    }
}

static Status compare_real(long double a, long double b, long double epsilon, int *result) {
    if (result == NULL || !isfinite(a) || !isfinite(b) || !isfinite(epsilon) || epsilon < 0.0L) {
        return STATUS_ARGUMENT;
    }
    if (fabsl(a - b) <= epsilon) {
        *result = 0;
    } else if (a < b) {
        *result = -1;
    } else {
        *result = 1;
    }
    return STATUS_OK;
}

static int compare_salary_exact(const void *a, const void *b) {
    const Employee *left = a;
    const Employee *right = b;
    int result = 0;
    if (compare_real(left->salary, right->salary, 0.0L, &result) != STATUS_OK) {
        return 0;
    }
    return result;
}

static int compare_names(const void *a, const void *b) {
    const Employee *left = a;
    const Employee *right = b;
    int order = strcmp(left->surname, right->surname);
    if (order == 0) {
        order = strcmp(left->name, right->name);
    }
    if (order != 0) {
        return order;
    }
    if (left->id < right->id) {
        return -1;
    }
    if (left->id > right->id) {
        return 1;
    }
    return 0;
}

static Status sort_employees(Employee *employees, size_t count, long double epsilon) {
    if ((employees == NULL && count != 0) || !isfinite(epsilon) || epsilon <= 0.0L) {
        return STATUS_ARGUMENT;
    }
    for (size_t i = 0; i < count; ++i) {
        if (validate_employee(&employees[i]) != STATUS_OK) {
            return STATUS_ARGUMENT;
        }
    }
    if (count < 2U) {
        return STATUS_OK;
    }
    qsort(employees, count, sizeof(*employees), compare_salary_exact);
    size_t first = 0;
    while (first < count) {
        size_t last = first + 1U;
        while (last < count && fabsl(employees[last].salary - employees[first].salary) <= epsilon) {
            ++last;
        }
        qsort(employees + first, last - first, sizeof(*employees), compare_names);
        first = last;
    }
    return STATUS_OK;
}

static Status write_employees(FILE *stream, const Employee *employees, size_t count, int descending) {
    if (stream == NULL || (employees == NULL && count != 0)) {
        return STATUS_ARGUMENT;
    }
    for (size_t i = 0; i < count; ++i) {
        size_t index = i;
        if (descending != 0) {
            index = count - 1U - i;
        }
        const Employee *employee = &employees[index];
        if (validate_employee(employee) != STATUS_OK) {
            return STATUS_ARGUMENT;
        }
        if (fprintf(stream, "%llu %s %s %.21Lg\n", employee->id, employee->name,
                    employee->surname, employee->salary) < 0) {
            return STATUS_IO;
        }
    }
    return STATUS_OK;
}

int main(int argc, char *argv[]) {
    if (argc != 4 || (argv[2][0] != '-' && argv[2][0] != '/') ||
        (argv[2][1] != 'a' && argv[2][1] != 'd') || argv[2][2] != '\0') {
        fprintf(stderr, "Использование: %s <входной файл> <-a|-d|/a|/d> <выходной файл>\n", argv[0]);
        return EXIT_FAILURE;
    }
    FILE *input = fopen(argv[1], "rb");
    if (input == NULL) {
        fputs("Ошибка открытия входного файла.\n", stderr);
        return EXIT_FAILURE;
    }
    Employee *employees = NULL;
    size_t count = 0;
    Status status = read_employees(input, &employees, &count);
    if (fclose(input) != 0) {
        status = STATUS_IO;
    }
    if (status == STATUS_OK) {
        status = sort_employees(employees, count, 1e-9L);
    }
    if (status == STATUS_OK) {
        FILE *output = fopen(argv[3], "wb");
        if (output == NULL) {
            status = STATUS_IO;
        } else {
            status = write_employees(output, employees, count, argv[2][1] == 'd');
            if (fclose(output) != 0) {
                status = STATUS_IO;
            }
        }
    }
    for (size_t i = 0; i < count; ++i) {
        free(employees[i].storage);
    }
    free(employees);
    if (status != STATUS_OK) {
        fprintf(stderr, "Ошибка (код %d).\n", (int)status);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
