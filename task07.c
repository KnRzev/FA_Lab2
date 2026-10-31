#include <math.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

typedef enum {
    STATUS_OK,
    STATUS_ARGUMENT,
    STATUS_MATRIX,
    STATUS_OVERFLOW,
    STATUS_MEMORY,
    STATUS_IO
} Status;

typedef struct {
    const double *coordinates;
} Vector;

typedef struct {
    double p;
    const double *matrix;
} NormParameters;

typedef Status (*NormFunction)(const Vector *, size_t, const NormParameters *, long double, long double *);

typedef struct {
    Vector *vectors;
    size_t count;
    long double length;
} NormResult;

static Status validate_vector(const Vector *vector, size_t dimension,
                              long double epsilon, long double *result) {
    if (vector == NULL || vector->coordinates == NULL || dimension == 0 || result == NULL ||
        !isfinite(epsilon) || epsilon <= 0.0L) {
        return STATUS_ARGUMENT;
    }
    for (size_t i = 0; i < dimension; ++i) {
        if (!isfinite(vector->coordinates[i])) {
            return STATUS_ARGUMENT;
        }
    }
    return STATUS_OK;
}

static Status infinity_norm(const Vector *vector, size_t dimension, const NormParameters *parameters,
                            long double epsilon, long double *result) {
    if (parameters == NULL) {
        return STATUS_ARGUMENT;
    }
    Status status = validate_vector(vector, dimension, epsilon, result);
    if (status != STATUS_OK) {
        return status;
    }
    long double maximum = 0.0L;
    for (size_t i = 0; i < dimension; ++i) {
        long double coordinate = fabsl((long double)vector->coordinates[i]);
        if (coordinate > maximum) {
            maximum = coordinate;
        }
    }
    *result = maximum;
    return STATUS_OK;
}

static Status p_norm(const Vector *vector, size_t dimension, const NormParameters *parameters,
                     long double epsilon, long double *result) {
    if (parameters == NULL || !isfinite(parameters->p) || parameters->p < 1.0 || result == NULL) {
        return STATUS_ARGUMENT;
    }
    long double maximum;
    Status status = infinity_norm(vector, dimension, parameters, epsilon, &maximum);
    if (status != STATUS_OK) {
        return status;
    }
    if (fpclassify(maximum) == FP_ZERO) {
        *result = 0.0L;
        return STATUS_OK;
    }
    long double sum = 0.0L;
    for (size_t i = 0; i < dimension; ++i) {
        long double coordinate = fabsl((long double)vector->coordinates[i]) / maximum;
        sum += powl(coordinate, (long double)parameters->p);
    }
    long double value = maximum * powl(sum, 1.0L / (long double)parameters->p);
    if (!isfinite(value)) {
        return STATUS_OVERFLOW;
    }
    *result = value;
    return STATUS_OK;
}

static Status matrix_norm(const Vector *vector, size_t dimension, const NormParameters *parameters,
                          long double epsilon, long double *result) {
    if (parameters == NULL || parameters->matrix == NULL) {
        return STATUS_ARGUMENT;
    }
    Status status = validate_vector(vector, dimension, epsilon, result);
    if (status != STATUS_OK) {
        return status;
    }
    if (dimension > SIZE_MAX / dimension || dimension * dimension > SIZE_MAX / sizeof(long double)) {
        return STATUS_OVERFLOW;
    }
    size_t cells = dimension * dimension;
    for (size_t i = 0; i < cells; ++i) {
        if (!isfinite(parameters->matrix[i])) {
            return STATUS_MATRIX;
        }
    }
    for (size_t i = 0; i < dimension; ++i) {
        for (size_t j = 0; j < i; ++j) {
            long double a = parameters->matrix[i * dimension + j];
            long double b = parameters->matrix[j * dimension + i];
            long double scale = fmaxl(1.0L, fmaxl(fabsl(a), fabsl(b)));
            if (fabsl(a - b) / scale > epsilon) {
                return STATUS_MATRIX;
            }
        }
    }
    long double *lower = calloc(cells, sizeof(*lower));
    if (lower == NULL) {
        return STATUS_MEMORY;
    }
    for (size_t i = 0; i < dimension && status == STATUS_OK; ++i) {
        for (size_t j = 0; j <= i; ++j) {
            long double sum = (long double)parameters->matrix[i * dimension + j] / 2.0L +
                              (long double)parameters->matrix[j * dimension + i] / 2.0L;
            for (size_t k = 0; k < j; ++k) {
                sum -= lower[i * dimension + k] * lower[j * dimension + k];
            }
            if (!isfinite(sum)) {
                status = STATUS_OVERFLOW;
                break;
            }
            if (i == j) {
                if (sum <= 0.0L) {
                    status = STATUS_MATRIX;
                    break;
                }
                lower[i * dimension + j] = sqrtl(sum);
            } else {
                lower[i * dimension + j] = sum / lower[j * dimension + j];
                if (!isfinite(lower[i * dimension + j])) {
                    status = STATUS_OVERFLOW;
                    break;
                }
            }
        }
    }
    long double norm = 0.0L;
    for (size_t j = 0; j < dimension && status == STATUS_OK; ++j) {
        long double component = 0.0L;
        for (size_t i = j; i < dimension; ++i) {
            component += lower[i * dimension + j] * vector->coordinates[i];
        }
        norm = hypotl(norm, component);
        if (!isfinite(norm)) {
            status = STATUS_OVERFLOW;
        }
    }
    free(lower);
    if (status == STATUS_OK) {
        *result = norm;
    }
    return status;
}

static Status longest_vectors(size_t dimension, long double epsilon, size_t vector_count,
                              size_t norm_count, NormResult **result, ...) {
    if (dimension == 0 || vector_count == 0 || norm_count == 0 || result == NULL ||
        !isfinite(epsilon) || epsilon <= 0.0L) {
        return STATUS_ARGUMENT;
    }
    *result = NULL;
    if (vector_count > SIZE_MAX / sizeof(Vector) || vector_count > SIZE_MAX / sizeof(long double) ||
        norm_count > SIZE_MAX / sizeof(NormResult)) {
        return STATUS_OVERFLOW;
    }
    Vector *vectors = malloc(vector_count * sizeof(*vectors));
    long double *lengths = malloc(vector_count * sizeof(*lengths));
    NormResult *answers = calloc(norm_count, sizeof(*answers));
    if (vectors == NULL || lengths == NULL || answers == NULL) {
        free(vectors);
        free(lengths);
        free(answers);
        return STATUS_MEMORY;
    }
    va_list args;
    va_start(args, result);
    Status status = STATUS_OK;
    for (size_t i = 0; i < vector_count; ++i) {
        vectors[i] = va_arg(args, Vector);
        status = validate_vector(&vectors[i], dimension, epsilon, &lengths[i]);
        if (status != STATUS_OK) {
            break;
        }
    }
    for (size_t i = 0; i < norm_count && status == STATUS_OK; ++i) {
        NormFunction function = va_arg(args, NormFunction);
        const NormParameters *parameters = va_arg(args, const NormParameters *);
        if (function == NULL || parameters == NULL) {
            status = STATUS_ARGUMENT;
            break;
        }
        long double maximum = 0.0L;
        for (size_t j = 0; j < vector_count; ++j) {
            status = function(&vectors[j], dimension, parameters, epsilon, &lengths[j]);
            if (status != STATUS_OK) {
                break;
            }
            if (!isfinite(lengths[j]) || lengths[j] < 0.0L) {
                status = STATUS_ARGUMENT;
                break;
            }
            if (lengths[j] > maximum) {
                maximum = lengths[j];
            }
        }
        if (status != STATUS_OK) {
            break;
        }
        answers[i].vectors = malloc(vector_count * sizeof(Vector));
        if (answers[i].vectors == NULL) {
            status = STATUS_MEMORY;
            break;
        }
        answers[i].length = maximum;
        for (size_t j = 0; j < vector_count; ++j) {
            if (fabsl(lengths[j] - maximum) <= epsilon) {
                answers[i].vectors[answers[i].count] = vectors[j];
                ++answers[i].count;
            }
        }
    }
    va_end(args);
    free(vectors);
    free(lengths);
    if (status != STATUS_OK) {
        for (size_t i = 0; i < norm_count; ++i) {
            free(answers[i].vectors);
        }
        free(answers);
    } else {
        *result = answers;
    }
    return status;
}

static Status print_results(const NormResult *results, size_t norms, size_t dimension) {
    if (results == NULL || norms == 0 || dimension == 0) {
        return STATUS_ARGUMENT;
    }
    for (size_t i = 0; i < norms; ++i) {
        if ((results[i].count != 0 && results[i].vectors == NULL) || !isfinite(results[i].length)) {
            return STATUS_ARGUMENT;
        }
        if (printf("norm=%zu maximum=%.12Lg vectors=%zu\n", i + 1U, results[i].length, results[i].count) < 0) {
            return STATUS_IO;
        }
        for (size_t j = 0; j < results[i].count; ++j) {
            if (results[i].vectors[j].coordinates == NULL) {
                return STATUS_ARGUMENT;
            }
            for (size_t k = 0; k < dimension; ++k) {
                if (k != 0 && putchar(' ') == EOF) {
                    return STATUS_IO;
                }
                if (printf("%.12g", results[i].vectors[j].coordinates[k]) < 0) {
                    return STATUS_IO;
                }
            }
            if (putchar('\n') == EOF) {
                return STATUS_IO;
            }
        }
    }
    return STATUS_OK;
}

int main(int argc, char *argv[]) {
    if (argc != 1) {
        fprintf(stderr, "Использование: %s\n", argv[0]);
        return EXIT_FAILURE;
    }
    const double first[] = {3.0, 4.0};
    const double second[] = {-3.0, -4.0};
    const double third[] = {5.0, 0.0};
    const double matrix[] = {2.0, 0.0, 0.0, 1.0};
    Vector a = {first};
    Vector b = {second};
    Vector c = {third};
    const NormParameters infinity = {0.0, NULL};
    const NormParameters euclidean = {2.0, NULL};
    const NormParameters weighted = {0.0, matrix};
    NormResult *results = NULL;
    Status status = longest_vectors(2U, 1e-12L, 3U, 3U, &results, a, b, c,
                                    infinity_norm, &infinity, p_norm, &euclidean, matrix_norm, &weighted);
    if (status == STATUS_OK) {
        status = print_results(results, 3U, 2U);
    }
    if (results != NULL) {
        for (size_t i = 0; i < 3U; ++i) {
            free(results[i].vectors);
        }
        free(results);
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
