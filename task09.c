#include <float.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    STATUS_OK,
    STATUS_ARGUMENT,
    STATUS_RANGE,
    STATUS_MEMORY,
    STATUS_IO
} Status;

static Status shortest_decimal(double value, char *result, size_t capacity) {
    if (result == NULL || capacity == 0 || !isfinite(value)) {
        return STATUS_ARGUMENT;
    }
    for (int precision = 1; precision <= DECIMAL_DIG; ++precision) {
        int length = snprintf(result, capacity, "%.*g", precision, value);
        if (length < 0) {
            return STATUS_IO;
        }
        if ((size_t)length >= capacity) {
            return STATUS_RANGE;
        }
        char *end;
        double restored = strtod(result, &end);
        if (*end == '\0' && memcmp(&restored, &value, sizeof(value)) == 0) {
            return STATUS_OK;
        }
    }
    return STATUS_RANGE;
}

static Status has_finite_representation(double value, unsigned int base, double epsilon, bool *result) {
    if (result == NULL || base < 2U || base > 36U || !isfinite(value) ||
        !isfinite(epsilon) || epsilon <= 0.0 || value <= 0.0 || value >= 1.0) {
        return STATUS_ARGUMENT;
    }
    char decimal[128];
    Status status = shortest_decimal(value, decimal, sizeof(decimal));
    if (status != STATUS_OK) {
        return status;
    }
    uint64_t numerator = 0;
    int fractional_digits = 0;
    bool after_dot = false;
    size_t position = 0;
    while (decimal[position] != '\0' && decimal[position] != 'e' && decimal[position] != 'E') {
        char c = decimal[position];
        if (c == '.') {
            after_dot = true;
        } else {
            if (c < '0' || c > '9') {
                return STATUS_RANGE;
            }
            unsigned int digit = (unsigned int)(c - '0');
            if (numerator > (UINT64_MAX - digit) / 10U) {
                return STATUS_RANGE;
            }
            numerator = numerator * 10U + digit;
            if (after_dot) {
                ++fractional_digits;
            }
        }
        ++position;
    }
    int exponent = 0;
    if (decimal[position] != '\0') {
        if (sscanf(decimal + position + 1U, "%d", &exponent) != 1) {
            return STATUS_RANGE;
        }
    }
    int denominator_power = fractional_digits - exponent;
    if (numerator == 0 || denominator_power <= 0) {
        return STATUS_RANGE;
    }
    int twos = denominator_power;
    int fives = denominator_power;
    while (twos > 0 && numerator % 2U == 0) {
        numerator /= 2U;
        --twos;
    }
    while (fives > 0 && numerator % 5U == 0) {
        numerator /= 5U;
        --fives;
    }
    *result = (twos == 0 || base % 2U == 0) && (fives == 0 || base % 5U == 0);
    return STATUS_OK;
}

static Status finite_fractions(unsigned int base, double epsilon, size_t count, bool **result, ...) {
    if (result == NULL || base < 2U || base > 36U || !isfinite(epsilon) || epsilon <= 0.0) {
        return STATUS_ARGUMENT;
    }
    *result = NULL;
    if (count == 0) {
        return STATUS_OK;
    }
    if (count > SIZE_MAX / sizeof(bool)) {
        return STATUS_RANGE;
    }
    bool *answers = malloc(count * sizeof(*answers));
    if (answers == NULL) {
        return STATUS_MEMORY;
    }
    va_list args;
    va_start(args, result);
    Status status = STATUS_OK;
    for (size_t i = 0; i < count && status == STATUS_OK; ++i) {
        double value = va_arg(args, double);
        status = has_finite_representation(value, base, epsilon, &answers[i]);
    }
    va_end(args);
    if (status != STATUS_OK) {
        free(answers);
    } else {
        *result = answers;
    }
    return status;
}

int main(int argc, char *argv[]) {
    if (argc != 1) {
        fprintf(stderr, "Использование: %s\n", argv[0]);
        return EXIT_FAILURE;
    }
    const double fractions[] = {0.5, 0.2, 0.125, 0.1};
    const unsigned int bases[] = {2U, 5U, 10U, 12U};
    Status status = STATUS_OK;
    for (size_t i = 0; i < sizeof(bases) / sizeof(bases[0]) && status == STATUS_OK; ++i) {
        bool *answers = NULL;
        status = finite_fractions(bases[i], 1e-12, 4U, &answers,
                                  fractions[0], fractions[1], fractions[2], fractions[3]);
        if (status == STATUS_OK) {
            for (size_t j = 0; j < 4U; ++j) {
                const char *text = "no";
                if (answers[j]) {
                    text = "yes";
                }
                if (printf("base=%u value=%.17g finite=%s\n", bases[i], fractions[j], text) < 0) {
                    status = STATUS_IO;
                    break;
                }
            }
        }
        free(answers);
    }
    if (status == STATUS_OK && fflush(stdout) == EOF) {
        status = STATUS_IO;
    }
    if (status != STATUS_OK) {
        fprintf(stderr, "Ошибка (код %d).\n", (int)status);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
