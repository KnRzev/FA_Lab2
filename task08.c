#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    STATUS_OK,
    STATUS_ARGUMENT,
    STATUS_DIGIT,
    STATUS_OVERFLOW,
    STATUS_MEMORY,
    STATUS_IO
} Status;

static Status digit_value(char c, unsigned int base, unsigned int *result) {
    if (result == NULL || base < 2U || base > 36U) {
        return STATUS_ARGUMENT;
    }
    unsigned int digit;
    if (c >= '0' && c <= '9') {
        digit = (unsigned int)(c - '0');
    } else if (c >= 'a' && c <= 'z') {
        digit = (unsigned int)(c - 'a') + 10U;
    } else if (c >= 'A' && c <= 'Z') {
        digit = (unsigned int)(c - 'A') + 10U;
    } else {
        return STATUS_DIGIT;
    }
    if (digit >= base) {
        return STATUS_DIGIT;
    }
    *result = digit;
    return STATUS_OK;
}

static Status add_numbers(const char *left, const char *right, unsigned int base, char **result) {
    if (result == NULL) {
        return STATUS_ARGUMENT;
    }
    *result = NULL;
    if (left == NULL || right == NULL || left[0] == '\0' || right[0] == '\0' ||
        base < 2U || base > 36U) {
        return STATUS_ARGUMENT;
    }
    size_t left_length = strlen(left);
    size_t right_length = strlen(right);
    size_t max_length = left_length;
    if (right_length > max_length) {
        max_length = right_length;
    }
    if (max_length > SIZE_MAX - 2U) {
        return STATUS_OVERFLOW;
    }
    char *sum = malloc(max_length + 2U);
    if (sum == NULL) {
        return STATUS_MEMORY;
    }
    const char alphabet[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    size_t position = max_length + 1U;
    sum[position] = '\0';
    unsigned int carry = 0;
    while (left_length != 0 || right_length != 0 || carry != 0) {
        unsigned int left_digit = 0;
        unsigned int right_digit = 0;
        Status status = STATUS_OK;
        if (left_length != 0) {
            --left_length;
            status = digit_value(left[left_length], base, &left_digit);
        }
        if (status == STATUS_OK && right_length != 0) {
            --right_length;
            status = digit_value(right[right_length], base, &right_digit);
        }
        if (status != STATUS_OK) {
            free(sum);
            return status;
        }
        unsigned int value = left_digit + right_digit + carry;
        --position;
        sum[position] = alphabet[value % base];
        carry = value / base;
    }
    while (sum[position] == '0' && sum[position + 1U] != '\0') {
        ++position;
    }
    memmove(sum, sum + position, max_length + 2U - position);
    *result = sum;
    return STATUS_OK;
}

static Status sum_numbers(unsigned int base, size_t count, char **result, ...) {
    if (result == NULL || base < 2U || base > 36U) {
        return STATUS_ARGUMENT;
    }
    *result = NULL;
    char *sum = malloc(2U);
    if (sum == NULL) {
        return STATUS_MEMORY;
    }
    sum[0] = '0';
    sum[1] = '\0';
    va_list args;
    va_start(args, result);
    Status status = STATUS_OK;
    for (size_t i = 0; i < count && status == STATUS_OK; ++i) {
        const char *number = va_arg(args, const char *);
        char *next = NULL;
        status = add_numbers(sum, number, base, &next);
        if (status == STATUS_OK) {
            free(sum);
            sum = next;
        }
    }
    va_end(args);
    if (status != STATUS_OK) {
        free(sum);
    } else {
        *result = sum;
    }
    return status;
}

int main(int argc, char *argv[]) {
    char *result = NULL;
    Status status = STATUS_OK;
    if (argc == 1) {
        const char *first = "FFFFFFFFFFFFFFFFFFFFFFFF";
        const char *second = "1";
        const char *third = "000A";
        status = sum_numbers(16U, 3U, &result, first, second, third);
    } else if (argc >= 3) {
        unsigned int base = 0;
        const char *text = argv[1];
        if (text[0] == '\0') {
            status = STATUS_ARGUMENT;
        }
        for (size_t i = 0; text[i] != '\0' && status == STATUS_OK; ++i) {
            if (text[i] < '0' || text[i] > '9') {
                status = STATUS_ARGUMENT;
            } else {
                base = base * 10U + (unsigned int)(text[i] - '0');
                if (base > 36U) {
                    status = STATUS_ARGUMENT;
                }
            }
        }
        if (base < 2U) {
            status = STATUS_ARGUMENT;
        }
        if (status == STATUS_OK) {
            status = sum_numbers(base, 0U, &result);
        }
        for (int i = 2; i < argc && status == STATUS_OK; ++i) {
            char *next = NULL;
            status = add_numbers(result, argv[i], base, &next);
            if (status == STATUS_OK) {
                free(result);
                result = next;
            }
        }
    } else {
        fprintf(stderr, "Использование: %s [<основание> <число> ...]\n", argv[0]);
        return EXIT_FAILURE;
    }
    if (status == STATUS_OK && puts(result) == EOF) {
        status = STATUS_IO;
    }
    free(result);
    if (status == STATUS_OK && fflush(stdout) == EOF) {
        status = STATUS_IO;
    }
    if (status != STATUS_OK) {
        fprintf(stderr, "Ошибка (код %d).\n", (int)status);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
