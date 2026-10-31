#include <float.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#if SIZE_MAX == UINT_MAX
typedef int SignedSize;
#elif SIZE_MAX == ULONG_MAX
typedef long SignedSize;
#else
typedef long long SignedSize;
#endif
#if PTRDIFF_MAX == INT_MAX
typedef unsigned int UnsignedDifference;
#elif PTRDIFF_MAX == LONG_MAX
typedef unsigned long UnsignedDifference;
#else
typedef unsigned long long UnsignedDifference;
#endif

typedef enum {
    STATUS_OK,
    STATUS_ARGUMENT,
    STATUS_FORMAT,
    STATUS_RANGE,
    STATUS_MEMORY,
    STATUS_IO
} Status;

typedef struct {
    FILE *stream;
    char *text;
    size_t length;
} Output;

typedef struct {
    size_t length;
    int stars;
    char modifier[3];
    char conversion;
} Spec;

static Status write_bytes(Output *output, const char *text, size_t length) {
    if (output == NULL || text == NULL ||
        (output->stream == NULL && output->text == NULL)) {
        return STATUS_ARGUMENT;
    }
    if (output->length > (size_t)INT_MAX || length > (size_t)INT_MAX - output->length) {
        return STATUS_RANGE;
    }
    if (output->stream != NULL) {
        if (fwrite(text, 1U, length, output->stream) != length) {
            return STATUS_IO;
        }
    } else {
        if (length != 0) {
            memcpy(output->text + output->length, text, length);
        }
        output->text[output->length + length] = '\0';
    }
    output->length += length;
    return STATUS_OK;
}

static Status roman_text(int number, char **result) {
    if (result == NULL) {
        return STATUS_ARGUMENT;
    }
    *result = NULL;
    unsigned int value = (unsigned int)number;
    if (number < 0) {
        value = 0U - value;
    }
    unsigned int thousands = value / 1000U;
    size_t capacity = (size_t)thousands + 16U;
    if (capacity < thousands) {
        return STATUS_RANGE;
    }
    char *text = malloc(capacity);
    if (text == NULL) {
        return STATUS_MEMORY;
    }
    size_t length = 0;
    if (number < 0) {
        text[length] = '-';
        ++length;
    }
    memset(text + length, 'M', thousands);
    length += thousands;
    value %= 1000U;
    if (number == 0) {
        text[length] = 'N';
        ++length;
    }
    const unsigned int values[] = {900U, 500U, 400U, 100U, 90U, 50U, 40U, 10U, 9U, 5U, 4U, 1U};
    const char *const symbols[] = {"CM", "D", "CD", "C", "XC", "L", "XL", "X", "IX", "V", "IV", "I"};
    for (size_t i = 0; i < 12U; ++i) {
        while (value >= values[i]) {
            size_t size = strlen(symbols[i]);
            memcpy(text + length, symbols[i], size);
            length += size;
            value -= values[i];
        }
    }
    text[length] = '\0';
    *result = text;
    return STATUS_OK;
}

static Status zeckendorf_text(unsigned int number, char *text, size_t capacity) {
    if (text == NULL) {
        return STATUS_ARGUMENT;
    }
    unsigned int fibonacci[sizeof(unsigned int) * CHAR_BIT * 2U];
    size_t count = 2U;
    fibonacci[0] = 1U;
    fibonacci[1] = 2U;
    while (fibonacci[count - 1U] <= UINT_MAX - fibonacci[count - 2U]) {
        fibonacci[count] = fibonacci[count - 1U] + fibonacci[count - 2U];
        ++count;
    }
    while (count > 1U && fibonacci[count - 1U] > number) {
        --count;
    }
    if (count + 2U > capacity) {
        return STATUS_RANGE;
    }
    memset(text, '0', count);
    for (size_t i = count; i > 0; --i) {
        if (fibonacci[i - 1U] <= number) {
            text[i - 1U] = '1';
            number -= fibonacci[i - 1U];
        }
    }
    text[count] = '1';
    text[count + 1U] = '\0';
    return STATUS_OK;
}

static Status base_text(int number, int base, int uppercase, char **result) {
    if (result == NULL) {
        return STATUS_ARGUMENT;
    }
    *result = NULL;
    if (base < 2 || base > 36) {
        base = 10;
    }
    const char *alphabet = "0123456789abcdefghijklmnopqrstuvwxyz";
    if (uppercase != 0) {
        alphabet = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    }
    char reversed[sizeof(int) * CHAR_BIT + 1U] = {0};
    size_t length = 0;
    unsigned int value = (unsigned int)number;
    if (number < 0) {
        value = 0U - value;
    }
    do {
        reversed[length] = alphabet[value % (unsigned int)base];
        ++length;
        value /= (unsigned int)base;
    } while (value != 0);
    if (number < 0) {
        reversed[length] = '-';
        ++length;
    }
    char *text = malloc(length + 1U);
    if (text == NULL) {
        return STATUS_MEMORY;
    }
    for (size_t i = 0; i < length; ++i) {
        text[i] = reversed[length - 1U - i];
    }
    text[length] = '\0';
    *result = text;
    return STATUS_OK;
}

static Status parse_base(const char *text, int base, int uppercase, int *result) {
    if (text == NULL || result == NULL || text[0] == '\0') {
        return STATUS_ARGUMENT;
    }
    if (base < 2 || base > 36) {
        base = 10;
    }
    int negative = text[0] == '-';
    size_t position = 0;
    unsigned int limit = (unsigned int)INT_MAX;
    if (negative != 0) {
        limit += 1U;
    }
    if (text[0] == '-' || text[0] == '+') {
        ++position;
    }
    if (text[position] == '\0') {
        return STATUS_FORMAT;
    }
    const char *alphabet = "0123456789abcdefghijklmnopqrstuvwxyz";
    if (uppercase != 0) {
        alphabet = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    }
    unsigned int value = 0;
    for (size_t i = position; text[i] != '\0'; ++i) {
        const char *found = strchr(alphabet, text[i]);
        if (found == NULL) {
            return STATUS_FORMAT;
        }
        unsigned int digit = (unsigned int)(found - alphabet);
        if (digit >= (unsigned int)base) {
            return STATUS_FORMAT;
        }
        if (value > (limit - digit) / (unsigned int)base) {
            return STATUS_RANGE;
        }
        value = value * (unsigned int)base + digit;
    }
    if (negative == 0) {
        *result = (int)value;
    } else if (value == (unsigned int)INT_MAX + 1U) {
        *result = INT_MIN;
    } else {
        *result = -(int)value;
    }
    return STATUS_OK;
}

static Status memory_text(const void *value, size_t size, char *text, size_t capacity) {
    if (value == NULL || size == 0 || text == NULL) {
        return STATUS_ARGUMENT;
    }
    if (size > capacity / (CHAR_BIT + 1U)) {
        return STATUS_RANGE;
    }
    const unsigned char *bytes = value;
    size_t position = 0;
    for (size_t i = 0; i < size; ++i) {
        if (i != 0) {
            text[position] = ' ';
            ++position;
        }
        for (unsigned int bit = CHAR_BIT; bit > 0; --bit) {
            if ((bytes[i] & (1U << (bit - 1U))) != 0) {
                text[position] = '1';
            } else {
                text[position] = '0';
            }
            ++position;
        }
    }
    text[position] = '\0';
    return STATUS_OK;
}

static Status parse_spec(const char *format, Spec *result) {
    if (format == NULL || format[0] != '%' || result == NULL) {
        return STATUS_ARGUMENT;
    }
    Spec spec = {0, 0, "", '\0'};
    size_t i = 1U + strspn(format + 1U, "-+ #0");
    if (format[i] == '*') {
        ++spec.stars;
        ++i;
    } else {
        i += strspn(format + i, "0123456789");
    }
    if (format[i] == '.') {
        ++i;
        if (format[i] == '*') {
            ++spec.stars;
            ++i;
        } else {
            i += strspn(format + i, "0123456789");
        }
    }
    if (format[i] != '\0' && strchr("hljztL", format[i]) != NULL) {
        spec.modifier[0] = format[i];
        ++i;
        if ((spec.modifier[0] == 'h' || spec.modifier[0] == 'l') && format[i] == spec.modifier[0]) {
            spec.modifier[1] = format[i];
            ++i;
        }
    }
    char c = format[i];
    if (c == '\0' || strchr("diouxXfFeEgGaAcspn", c) == NULL) {
        return STATUS_FORMAT;
    }
    int integer = strchr("diouxXn", c) != NULL;
    int floating = strchr("fFeEgGaA", c) != NULL;
    char modifier = spec.modifier[0];
    if ((integer && modifier == 'L') ||
        (floating && modifier != '\0' && (spec.modifier[1] != '\0' || (modifier != 'l' && modifier != 'L'))) ||
        ((c == 'c' || c == 's') && modifier != '\0' && (modifier != 'l' || spec.modifier[1] != '\0')) ||
        (c == 'p' && modifier != '\0') || (c == 'n' && i != 1U + strlen(spec.modifier))) {
        return STATUS_FORMAT;
    }
    spec.length = i + 1U;
    spec.conversion = c;
    *result = spec;
    return STATUS_OK;
}

static Status store_count(va_list *args, const char *modifier, size_t count) {
    if (args == NULL || modifier == NULL || count > (size_t)INT_MAX) {
        return STATUS_ARGUMENT;
    }
    if (strcmp(modifier, "hh") == 0) {
        signed char *target = va_arg(*args, signed char *);
        if (target == NULL) {
            return STATUS_ARGUMENT;
        }
        if (count > SCHAR_MAX) {
            return STATUS_RANGE;
        }
        *target = (signed char)count;
    } else if (strcmp(modifier, "h") == 0) {
        short *target = va_arg(*args, short *);
        if (target == NULL) {
            return STATUS_ARGUMENT;
        }
        if (count > SHRT_MAX) {
            return STATUS_RANGE;
        }
        *target = (short)count;
    } else if (strcmp(modifier, "ll") == 0) {
        long long *target = va_arg(*args, long long *);
        if (target == NULL) {
            return STATUS_ARGUMENT;
        }
        *target = (long long)count;
    } else if (strcmp(modifier, "l") == 0) {
        long *target = va_arg(*args, long *);
        if (target == NULL) {
            return STATUS_ARGUMENT;
        }
        *target = (long)count;
    } else if (strcmp(modifier, "j") == 0) {
        intmax_t *target = va_arg(*args, intmax_t *);
        if (target == NULL) {
            return STATUS_ARGUMENT;
        }
        *target = (intmax_t)count;
    } else if (strcmp(modifier, "z") == 0) {
        SignedSize *target = va_arg(*args, SignedSize *);
        if (target == NULL) {
            return STATUS_ARGUMENT;
        }
        *target = (SignedSize)count;
    } else if (strcmp(modifier, "t") == 0) {
        ptrdiff_t *target = va_arg(*args, ptrdiff_t *);
        if (target == NULL) {
            return STATUS_ARGUMENT;
        }
        *target = (ptrdiff_t)count;
    } else {
        int *target = va_arg(*args, int *);
        if (target == NULL) {
            return STATUS_ARGUMENT;
        }
        *target = (int)count;
    }
    return STATUS_OK;
}

static Status consume_value(va_list *args, const Spec *spec) {
    if (args == NULL || spec == NULL) {
        return STATUS_ARGUMENT;
    }
    for (int i = 0; i < spec->stars; ++i) {
        (void)va_arg(*args, int);
    }
    char c = spec->conversion;
    const char *m = spec->modifier;
    if (c == 'd' || c == 'i') {
        if (strcmp(m, "ll") == 0) {
            (void)va_arg(*args, long long);
        } else if (strcmp(m, "l") == 0) {
            (void)va_arg(*args, long);
        } else if (strcmp(m, "j") == 0) {
            (void)va_arg(*args, intmax_t);
        } else if (strcmp(m, "z") == 0) {
            (void)va_arg(*args, SignedSize);
        } else if (strcmp(m, "t") == 0) {
            (void)va_arg(*args, ptrdiff_t);
        } else {
            (void)va_arg(*args, int);
        }
    } else if (strchr("ouxX", c) != NULL) {
        if (strcmp(m, "ll") == 0) {
            (void)va_arg(*args, unsigned long long);
        } else if (strcmp(m, "l") == 0) {
            (void)va_arg(*args, unsigned long);
        } else if (strcmp(m, "j") == 0) {
            (void)va_arg(*args, uintmax_t);
        } else if (strcmp(m, "z") == 0) {
            (void)va_arg(*args, size_t);
        } else if (strcmp(m, "t") == 0) {
            (void)va_arg(*args, UnsignedDifference);
        } else {
            (void)va_arg(*args, unsigned int);
        }
    } else if (strchr("fFeEgGaA", c) != NULL) {
        if (m[0] == 'L') {
            (void)va_arg(*args, long double);
        } else {
            (void)va_arg(*args, double);
        }
    } else if (c == 'c') {
        if (m[0] == 'l') {
            (void)va_arg(*args, wint_t);
        } else {
            (void)va_arg(*args, int);
        }
    } else if (c == 's') {
        if (m[0] == 'l') {
            if (va_arg(*args, const wchar_t *) == NULL) {
                return STATUS_ARGUMENT;
            }
        } else if (va_arg(*args, const char *) == NULL) {
            return STATUS_ARGUMENT;
        }
    } else if (c == 'p') {
        (void)va_arg(*args, void *);
    } else {
        return STATUS_FORMAT;
    }
    return STATUS_OK;
}

static Status format_standard(Output *output, const char *format, const Spec *spec, va_list *args) {
    if (output == NULL || format == NULL || spec == NULL || args == NULL ||
        (output->stream == NULL && output->text == NULL)) {
        return STATUS_ARGUMENT;
    }
    if (output->length > (size_t)INT_MAX) {
        return STATUS_RANGE;
    }
    if (spec->conversion == 'n') {
        return store_count(args, spec->modifier, output->length);
    }
    char *token = malloc(spec->length + 1U);
    if (token == NULL) {
        return STATUS_MEMORY;
    }
    memcpy(token, format, spec->length);
    token[spec->length] = '\0';
    va_list values;
    va_copy(values, *args);
    Status status = consume_value(args, spec);
    if (status == STATUS_OK) {
        int written;
        if (output->stream != NULL) {
            written = vfprintf(output->stream, token, values);
        } else {
            written = vsprintf(output->text + output->length, token, values);
        }
        if (written < 0) {
            status = STATUS_IO;
        } else if ((size_t)written > (size_t)INT_MAX - output->length) {
            status = STATUS_RANGE;
        } else {
            output->length += (size_t)written;
        }
    }
    va_end(values);
    free(token);
    return status;
}

static Status format_custom(const char *flag, va_list *args, Output *output, int *recognized) {
    if (flag == NULL || args == NULL || output == NULL || recognized == NULL) {
        return STATUS_ARGUMENT;
    }
    *recognized = 1;
    if (flag[0] == 'R' && flag[1] == 'o') {
        char *text = NULL;
        Status status = roman_text(va_arg(*args, int), &text);
        if (status == STATUS_OK) {
            status = write_bytes(output, text, strlen(text));
        }
        free(text);
        return status;
    }
    char text[2U * sizeof(int) * CHAR_BIT + (sizeof(double) + sizeof(float)) * (CHAR_BIT + 1U) + 2U] = {0};
    char *allocated = NULL;
    const char *result = text;
    Status status = STATUS_OK;
    if (flag[0] == 'Z' && flag[1] == 'r') {
        status = zeckendorf_text(va_arg(*args, unsigned int), text, sizeof(text));
    } else if (flag[0] == 'C' && (flag[1] == 'v' || flag[1] == 'V')) {
        int value = va_arg(*args, int);
        int base = va_arg(*args, int);
        status = base_text(value, base, flag[1] == 'V', &allocated);
        result = allocated;
    } else if ((flag[0] == 't' && flag[1] == 'o') || (flag[0] == 'T' && flag[1] == 'O')) {
        const char *number = va_arg(*args, const char *);
        int base = va_arg(*args, int);
        int value;
        status = parse_base(number, base, flag[0] == 'T', &value);
        if (status == STATUS_OK) {
            status = base_text(value, 10, 0, &allocated);
            result = allocated;
        }
    } else if (flag[0] == 'm') {
        if (flag[1] == 'i') {
            int value = va_arg(*args, int);
            if (sizeof(value) != 4U || CHAR_BIT != 8) {
                return STATUS_RANGE;
            }
            status = memory_text(&value, sizeof(value), text, sizeof(text));
        } else if (flag[1] == 'u') {
            unsigned int value = va_arg(*args, unsigned int);
            if (sizeof(value) != 4U || CHAR_BIT != 8) {
                return STATUS_RANGE;
            }
            status = memory_text(&value, sizeof(value), text, sizeof(text));
        } else if (flag[1] == 'd') {
            double value = va_arg(*args, double);
            status = memory_text(&value, sizeof(value), text, sizeof(text));
        } else if (flag[1] == 'f') {
            double promoted = va_arg(*args, double);
            if (isfinite(promoted) && fabs(promoted) > FLT_MAX) {
                return STATUS_RANGE;
            }
            float value = (float)promoted;
            status = memory_text(&value, sizeof(value), text, sizeof(text));
        } else {
            *recognized = 0;
        }
    } else {
        *recognized = 0;
    }
    if (status == STATUS_OK && *recognized != 0) {
        status = write_bytes(output, result, strlen(result));
    }
    free(allocated);
    return status;
}

static Status build_output(Output *output, const char *format, va_list *args) {
    if (output == NULL || format == NULL || args == NULL) {
        return STATUS_ARGUMENT;
    }
    Status status = STATUS_OK;
    size_t i = 0;
    while (format[i] != '\0' && status == STATUS_OK) {
        if (format[i] != '%') {
            status = write_bytes(output, format + i, 1U);
            ++i;
        } else if (format[i + 1U] == '%') {
            status = write_bytes(output, "%", 1U);
            i += 2U;
        } else {
            int custom = 0;
            status = format_custom(format + i + 1U, args, output, &custom);
            if (status == STATUS_OK && custom != 0) {
                i += 3U;
            } else if (status == STATUS_OK) {
                Spec spec;
                status = parse_spec(format + i, &spec);
                if (status == STATUS_OK) {
                    status = format_standard(output, format + i, &spec, args);
                    i += spec.length;
                }
            }
        }
    }
    return status;
}

int overfprintf(FILE *stream, const char *format, ...) {
    if (stream == NULL || format == NULL) {
        return -1;
    }
    Output output = {stream, NULL, 0};
    va_list args;
    va_start(args, format);
    Status status = build_output(&output, format, &args);
    va_end(args);
    if (status != STATUS_OK) {
        return -1;
    }
    return (int)output.length;
}

int oversprintf(char *destination, const char *format, ...) {
    if (destination == NULL || format == NULL) {
        return -1;
    }
    destination[0] = '\0';
    Output output = {NULL, destination, 0};
    va_list args;
    va_start(args, format);
    Status status = build_output(&output, format, &args);
    va_end(args);
    if (status != STATUS_OK) {
        destination[0] = '\0';
        return -1;
    }
    return (int)output.length;
}

int main(int argc, char *argv[]) {
    if (argc != 1) {
        fprintf(stderr, "Использование: %s\n", argv[0]);
        return EXIT_FAILURE;
    }
    char text[1024];
    int count = oversprintf(text, "Roman=%Ro Zeckendorf=%Zr lower=%Cv upper=%CV decimal=%to/%TO",
                           2026, 100U, -255, 16, 255, 16, "ff", 16, "FF", 16);
    if (count < 0 || puts(text) == EOF) {
        fputs("Ошибка форматирования.\n", stderr);
        return EXIT_FAILURE;
    }
    int printed = overfprintf(stdout, "int=%mi\nuint=%mu\ndouble=%md\nfloat=%mf\nstandard=%+08d %.3f %s %%\n",
                              -42, 42U, 0.5, (double)0.5f, 42, 3.14159, "OK");
    if (printed < 0 || fflush(stdout) == EOF) {
        fputs("Ошибка вывода.\n", stderr);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
