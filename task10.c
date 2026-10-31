#include <math.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

typedef enum {
    STATUS_OK,
    STATUS_ARGUMENT,
    STATUS_OVERFLOW,
    STATUS_MEMORY,
    STATUS_IO
} Status;

static Status shift_polynomial(double epsilon, double a, long double **result, int degree, ...) {
    if (result == NULL || degree < 0 || !isfinite(epsilon) || epsilon <= 0.0 || !isfinite(a)) {
        return STATUS_ARGUMENT;
    }
    *result = NULL;
    size_t count = (size_t)degree + 1U;
    if (count > SIZE_MAX / sizeof(long double)) {
        return STATUS_OVERFLOW;
    }
    long double *coefficients = malloc(count * sizeof(*coefficients));
    if (coefficients == NULL) {
        return STATUS_MEMORY;
    }
    Status status = STATUS_OK;
    va_list args;
    va_start(args, degree);
    for (size_t i = 0; i < count; ++i) {
        double coefficient = va_arg(args, double);
        if (!isfinite(coefficient)) {
            status = STATUS_ARGUMENT;
            break;
        }
        coefficients[i] = coefficient;
    }
    va_end(args);
    for (size_t step = count - 1U; step > 0 && status == STATUS_OK; --step) {
        for (size_t j = step - 1U; j + 1U < count; ++j) {
            coefficients[j] += (long double)a * coefficients[j + 1U];
            if (!isfinite(coefficients[j])) {
                status = STATUS_OVERFLOW;
                break;
            }
        }
    }
    if (status != STATUS_OK) {
        free(coefficients);
    } else {
        *result = coefficients;
    }
    return status;
}

int main(int argc, char *argv[]) {
    if (argc != 1) {
        fprintf(stderr, "Использование: %s\n", argv[0]);
        return EXIT_FAILURE;
    }
    long double *coefficients = NULL;
    Status status = shift_polynomial(1e-12, 2.0, &coefficients, 3, 1.0, -3.0, 2.0, 1.0);
    if (status == STATUS_OK) {
        for (size_t i = 0; i < 4U; ++i) {
            if (printf("g%zu = %.18Lg\n", i, coefficients[i]) < 0) {
                status = STATUS_IO;
                break;
            }
        }
    }
    free(coefficients);
    if (status == STATUS_OK && fflush(stdout) == EOF) {
        status = STATUS_IO;
    }
    if (status != STATUS_OK) {
        fprintf(stderr, "Ошибка (код %d).\n", (int)status);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
