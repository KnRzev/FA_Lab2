#include <ctype.h>
#include <inttypes.h>
#include <limits.h>
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
    FILE *file;
    const char *text;
    size_t position;
    size_t consumed;
} Input;

typedef struct {
    size_t length;
    int suppressed;
    char modifier[3];
    char conversion;
} Spec;

typedef struct {
    int matched;
    int assigned;
    int input_failure;
} Outcome;

static Status input_get(Input *input, int *result) {
    if (input == NULL || result == NULL || (input->file == NULL && input->text == NULL)) {
        return STATUS_ARGUMENT;
    }
    if (input->consumed == SIZE_MAX) {
        return STATUS_RANGE;
    }
    int c;
    if (input->file != NULL) {
        c = fgetc(input->file);
        if (c == EOF && ferror(input->file)) {
            return STATUS_IO;
        }
    } else {
        c = (unsigned char)input->text[input->position];
        if (c == '\0') {
            c = EOF;
        } else {
            ++input->position;
        }
    }
    if (c != EOF) {
        ++input->consumed;
    }
    *result = c;
    return STATUS_OK;
}

static Status input_unget(Input *input, int c) {
    if (input == NULL || c == EOF || input->consumed == 0) {
        return STATUS_ARGUMENT;
    }
    if (input->file != NULL) {
        if (ungetc(c, input->file) == EOF) {
            return STATUS_IO;
        }
    } else {
        --input->position;
    }
    --input->consumed;
    return STATUS_OK;
}

static Status skip_space(Input *input) {
    if (input == NULL) {
        return STATUS_ARGUMENT;
    }
    for (;;) {
        int c;
        Status status = input_get(input, &c);
        if (status != STATUS_OK) {
            return status;
        }
        if (c == EOF) {
            return STATUS_OK;
        }
        if (!isspace((unsigned char)c)) {
            return input_unget(input, c);
        }
    }
}

static Status parse_spec(const char *format, Spec *result) {
    if (format == NULL || format[0] != '%' || result == NULL) {
        return STATUS_ARGUMENT;
    }
    Spec spec = {0, 0, "", '\0'};
    size_t i = 1;
    if (format[i] == '*') {
        spec.suppressed = 1;
        ++i;
    }
    int has_width = 0;
    if (isdigit((unsigned char)format[i])) {
        has_width = 1;
        if (format[i] == '0') {
            return STATUS_FORMAT;
        }
        unsigned int width = 0;
        while (isdigit((unsigned char)format[i])) {
            unsigned int digit = (unsigned int)(format[i] - '0');
            if (width > ((unsigned int)INT_MAX - digit) / 10U) {
                return STATUS_RANGE;
            }
            width = width * 10U + digit;
            ++i;
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
    if (c == '\0' || strchr("diouxXfFeEgGaAcspn[", c) == NULL) {
        return STATUS_FORMAT;
    }
    if (strchr("diouxXn", c) != NULL && spec.modifier[0] == 'L') {
        return STATUS_FORMAT;
    }
    if (strchr("fFeEgGaA", c) != NULL && spec.modifier[0] != '\0' &&
        strcmp(spec.modifier, "l") != 0 && strcmp(spec.modifier, "L") != 0) {
        return STATUS_FORMAT;
    }
    if ((c == 'c' || c == 's' || c == '[') && spec.modifier[0] != '\0' &&
        strcmp(spec.modifier, "l") != 0) {
        return STATUS_FORMAT;
    }
    if ((c == 'p' && spec.modifier[0] != '\0') ||
        (c == 'n' && (has_width || spec.suppressed))) {
        return STATUS_FORMAT;
    }
    if (c == '[') {
        ++i;
        if (format[i] == '^') {
            ++i;
        }
        if (format[i] == ']') {
            ++i;
        }
        while (format[i] != '\0' && format[i] != ']') {
            ++i;
        }
        if (format[i] != ']') {
            return STATUS_FORMAT;
        }
    }
    spec.conversion = c;
    spec.length = i + 1U;
    *result = spec;
    return STATUS_OK;
}

static Status store_count(va_list *args, const char *modifier, size_t count) {
    if (args == NULL || modifier == NULL) {
        return STATUS_ARGUMENT;
    }
    if (strcmp(modifier, "hh") == 0) {
        signed char *target = va_arg(*args, signed char *);
        if (target == NULL) {
            return STATUS_ARGUMENT;
        }
        if (count > (uintmax_t)(SCHAR_MAX)) {
            return STATUS_RANGE;
        }
        *target = (signed char)count;
    } else if (strcmp(modifier, "h") == 0) {
        short *target = va_arg(*args, short *);
        if (target == NULL) {
            return STATUS_ARGUMENT;
        }
        if (count > (uintmax_t)(SHRT_MAX)) {
            return STATUS_RANGE;
        }
        *target = (short)count;
    } else if (strcmp(modifier, "ll") == 0) {
        long long *target = va_arg(*args, long long *);
        if (target == NULL) {
            return STATUS_ARGUMENT;
        }
        if (count > (uintmax_t)(LLONG_MAX)) {
            return STATUS_RANGE;
        }
        *target = (long long)count;
    } else if (strcmp(modifier, "l") == 0) {
        long *target = va_arg(*args, long *);
        if (target == NULL) {
            return STATUS_ARGUMENT;
        }
        if (count > (uintmax_t)(LONG_MAX)) {
            return STATUS_RANGE;
        }
        *target = (long)count;
    } else if (strcmp(modifier, "j") == 0) {
        intmax_t *target = va_arg(*args, intmax_t *);
        if (target == NULL) {
            return STATUS_ARGUMENT;
        }
        if (count > (uintmax_t)(INTMAX_MAX)) {
            return STATUS_RANGE;
        }
        *target = (intmax_t)count;
    } else if (strcmp(modifier, "z") == 0) {
        SignedSize *target = va_arg(*args, SignedSize *);
        if (target == NULL) {
            return STATUS_ARGUMENT;
        }
        if (count > (uintmax_t)(SIZE_MAX / 2U)) {
            return STATUS_RANGE;
        }
        *target = (SignedSize)count;
    } else if (strcmp(modifier, "t") == 0) {
        ptrdiff_t *target = va_arg(*args, ptrdiff_t *);
        if (target == NULL) {
            return STATUS_ARGUMENT;
        }
        if (count > (uintmax_t)(PTRDIFF_MAX)) {
            return STATUS_RANGE;
        }
        *target = (ptrdiff_t)count;
    } else {
        int *target = va_arg(*args, int *);
        if (target == NULL) {
            return STATUS_ARGUMENT;
        }
        if (count > (uintmax_t)(INT_MAX)) {
            return STATUS_RANGE;
        }
        *target = (int)count;
    }
    return STATUS_OK;
}

static Status scan_value(Input *input, const char *format, int *converted, ...) {
    if (input == NULL || format == NULL || converted == NULL ||
        (input->file == NULL && input->text == NULL)) {
        return STATUS_ARGUMENT;
    }
    va_list args;
    va_start(args, converted);
    if (input->file != NULL) {
        *converted = vfscanf(input->file, format, args);
    } else {
        *converted = vsscanf(input->text + input->position, format, args);
    }
    va_end(args);
    if (input->file != NULL && ferror(input->file)) {
        return STATUS_IO;
    }
    return STATUS_OK;
}

static Status scan_standard(Input *input, const char *format, const Spec *spec,
                            va_list *args, Outcome *outcome) {
    if (input == NULL || format == NULL || spec == NULL || args == NULL || outcome == NULL) {
        return STATUS_ARGUMENT;
    }
    *outcome = (Outcome){0, 0, 0};
    char *token = malloc(spec->length + 3U);
    if (token == NULL) {
        return STATUS_MEMORY;
    }
    memcpy(token, format, spec->length);
    memcpy(token + spec->length, "%n", 3U);
    int consumed = -1;
    int converted = 0;
    Status status = STATUS_OK;
    const char *modifier = spec->modifier;
    char conversion = spec->conversion;
    if (spec->suppressed) {
        status = scan_value(input, token, &converted, &consumed);
    } else if (conversion == 'd' || conversion == 'i') {
        if (strcmp(modifier, "hh") == 0) {
            signed char *target = va_arg(*args, signed char *);
            if (target == NULL) {
                status = STATUS_ARGUMENT;
            } else {
                status = scan_value(input, token, &converted, target, &consumed);
            }
        } else if (strcmp(modifier, "h") == 0) {
            short *target = va_arg(*args, short *);
            if (target == NULL) {
                status = STATUS_ARGUMENT;
            } else {
                status = scan_value(input, token, &converted, target, &consumed);
            }
        } else if (strcmp(modifier, "ll") == 0) {
            long long *target = va_arg(*args, long long *);
            if (target == NULL) {
                status = STATUS_ARGUMENT;
            } else {
                status = scan_value(input, token, &converted, target, &consumed);
            }
        } else if (strcmp(modifier, "l") == 0) {
            long *target = va_arg(*args, long *);
            if (target == NULL) {
                status = STATUS_ARGUMENT;
            } else {
                status = scan_value(input, token, &converted, target, &consumed);
            }
        } else if (strcmp(modifier, "j") == 0) {
            intmax_t *target = va_arg(*args, intmax_t *);
            if (target == NULL) {
                status = STATUS_ARGUMENT;
            } else {
                status = scan_value(input, token, &converted, target, &consumed);
            }
        } else if (strcmp(modifier, "z") == 0) {
            SignedSize *target = va_arg(*args, SignedSize *);
            if (target == NULL) {
                status = STATUS_ARGUMENT;
            } else {
                status = scan_value(input, token, &converted, target, &consumed);
            }
        } else if (strcmp(modifier, "t") == 0) {
            ptrdiff_t *target = va_arg(*args, ptrdiff_t *);
            if (target == NULL) {
                status = STATUS_ARGUMENT;
            } else {
                status = scan_value(input, token, &converted, target, &consumed);
            }
        } else {
            int *target = va_arg(*args, int *);
            if (target == NULL) {
                status = STATUS_ARGUMENT;
            } else {
                status = scan_value(input, token, &converted, target, &consumed);
            }
        }
    } else if (strchr("ouxX", conversion) != NULL) {
        if (strcmp(modifier, "hh") == 0) {
            unsigned char *target = va_arg(*args, unsigned char *);
            if (target == NULL) {
                status = STATUS_ARGUMENT;
            } else {
                status = scan_value(input, token, &converted, target, &consumed);
            }
        } else if (strcmp(modifier, "h") == 0) {
            unsigned short *target = va_arg(*args, unsigned short *);
            if (target == NULL) {
                status = STATUS_ARGUMENT;
            } else {
                status = scan_value(input, token, &converted, target, &consumed);
            }
        } else if (strcmp(modifier, "ll") == 0) {
            unsigned long long *target = va_arg(*args, unsigned long long *);
            if (target == NULL) {
                status = STATUS_ARGUMENT;
            } else {
                status = scan_value(input, token, &converted, target, &consumed);
            }
        } else if (strcmp(modifier, "l") == 0) {
            unsigned long *target = va_arg(*args, unsigned long *);
            if (target == NULL) {
                status = STATUS_ARGUMENT;
            } else {
                status = scan_value(input, token, &converted, target, &consumed);
            }
        } else if (strcmp(modifier, "j") == 0) {
            uintmax_t *target = va_arg(*args, uintmax_t *);
            if (target == NULL) {
                status = STATUS_ARGUMENT;
            } else {
                status = scan_value(input, token, &converted, target, &consumed);
            }
        } else if (strcmp(modifier, "z") == 0) {
            size_t *target = va_arg(*args, size_t *);
            if (target == NULL) {
                status = STATUS_ARGUMENT;
            } else {
                status = scan_value(input, token, &converted, target, &consumed);
            }
        } else if (strcmp(modifier, "t") == 0) {
            UnsignedDifference *target = va_arg(*args, UnsignedDifference *);
            if (target == NULL) {
                status = STATUS_ARGUMENT;
            } else {
                status = scan_value(input, token, &converted, target, &consumed);
            }
        } else {
            unsigned int *target = va_arg(*args, unsigned int *);
            if (target == NULL) {
                status = STATUS_ARGUMENT;
            } else {
                status = scan_value(input, token, &converted, target, &consumed);
            }
        }
    } else if (strchr("fFeEgGaA", conversion) != NULL) {
        if (modifier[0] == 'L') {
            long double *target = va_arg(*args, long double *);
            if (target == NULL) {
                status = STATUS_ARGUMENT;
            } else {
                status = scan_value(input, token, &converted, target, &consumed);
            }
        } else if (modifier[0] == 'l') {
            double *target = va_arg(*args, double *);
            if (target == NULL) {
                status = STATUS_ARGUMENT;
            } else {
                status = scan_value(input, token, &converted, target, &consumed);
            }
        } else {
            float *target = va_arg(*args, float *);
            if (target == NULL) {
                status = STATUS_ARGUMENT;
            } else {
                status = scan_value(input, token, &converted, target, &consumed);
            }
        }
    } else if (conversion == 'c' || conversion == 's' || conversion == '[') {
        if (modifier[0] == 'l') {
            wchar_t *target = va_arg(*args, wchar_t *);
            if (target == NULL) {
                status = STATUS_ARGUMENT;
            } else {
                status = scan_value(input, token, &converted, target, &consumed);
            }
        } else {
            char *target = va_arg(*args, char *);
            if (target == NULL) {
                status = STATUS_ARGUMENT;
            } else {
                status = scan_value(input, token, &converted, target, &consumed);
            }
        }
    } else if (conversion == 'p') {
        void * *target = va_arg(*args, void * *);
        if (target == NULL) {
            status = STATUS_ARGUMENT;
        } else {
            status = scan_value(input, token, &converted, target, &consumed);
        }
    } else {
        status = STATUS_FORMAT;
    }
    free(token);
    if (status != STATUS_OK) {
        return status;
    }
    if (consumed < 0) {
        outcome->input_failure = converted == EOF;
        return STATUS_OK;
    }
    if ((size_t)consumed > SIZE_MAX - input->consumed) {
        return STATUS_RANGE;
    }
    input->consumed += (size_t)consumed;
    if (input->file == NULL) {
        input->position += (size_t)consumed;
    }
    outcome->matched = 1;
    if (!spec->suppressed) {
        outcome->assigned = 1;
    }
    return STATUS_OK;
}

static Status digit_value(int c, int base, int uppercase, unsigned int *result) {
    if (base < 2 || base > 36 || result == NULL) {
        return STATUS_ARGUMENT;
    }
    int digit;
    if (c >= '0' && c <= '9') {
        digit = c - '0';
    } else if (!uppercase && c >= 'a' && c <= 'z') {
        digit = c - 'a' + 10;
    } else if (uppercase && c >= 'A' && c <= 'Z') {
        digit = c - 'A' + 10;
    } else {
        return STATUS_FORMAT;
    }
    if (digit >= base) {
        return STATUS_FORMAT;
    }
    *result = (unsigned int)digit;
    return STATUS_OK;
}

static Status read_custom_token(Input *input, char kind, int base,
                                char **result, int *input_failure) {
    if (input == NULL || result == NULL || input_failure == NULL) {
        return STATUS_ARGUMENT;
    }
    *result = NULL;
    *input_failure = 0;
    Status status = skip_space(input);
    if (status != STATUS_OK) {
        return status;
    }
    size_t length = 0;
    size_t capacity = 32U;
    char *text = malloc(capacity);
    if (text == NULL) {
        return STATUS_MEMORY;
    }
    int c = EOF;
    for (;;) {
        status = input_get(input, &c);
        if (status != STATUS_OK || c == EOF) {
            break;
        }
        int valid;
        if (length == 0 && (c == '+' || c == '-') && kind != 'Z') {
            valid = 1;
        } else if (kind == 'R') {
            valid = c != '\0' && strchr("IVXLCDMN", c) != NULL;
        } else if (kind == 'Z') {
            valid = c == '0' || c == '1';
        } else {
            unsigned int digit;
            valid = digit_value(c, base, kind == 'C', &digit) == STATUS_OK;
        }
        if (!valid) {
            status = input_unget(input, c);
            break;
        }
        if (length == capacity - 1U) {
            if (capacity > (size_t)INT_MAX / 2U) {
                status = STATUS_RANGE;
                break;
            }
            char *grown = realloc(text, capacity * 2U);
            if (grown == NULL) {
                status = STATUS_MEMORY;
                break;
            }
            text = grown;
            capacity *= 2U;
        }
        text[length] = (char)c;
        ++length;
    }
    if (length == 0 && c == EOF) {
        *input_failure = 1;
    }
    if (status != STATUS_OK) {
        free(text);
        return status;
    }
    text[length] = '\0';
    *result = text;
    return STATUS_OK;
}

static Status parse_base(const char *text, int base, int uppercase, int *result) {
    if (text == NULL || result == NULL || text[0] == '\0') {
        return STATUS_FORMAT;
    }
    int negative = text[0] == '-';
    if (text[0] == '-' || text[0] == '+') {
        ++text;
    }
    if (text[0] == '\0') {
        return STATUS_FORMAT;
    }
    unsigned int limit = (unsigned int)INT_MAX;
    if (negative) {
        ++limit;
    }
    unsigned int number = 0;
    for (size_t i = 0; text[i] != '\0'; ++i) {
        unsigned int digit;
        Status status = digit_value((unsigned char)text[i], base, uppercase, &digit);
        if (status != STATUS_OK) {
            return status;
        }
        if (number > (limit - digit) / (unsigned int)base) {
            return STATUS_RANGE;
        }
        number = number * (unsigned int)base + digit;
    }
    if (!negative) {
        *result = (int)number;
    } else if (number == (unsigned int)INT_MAX + 1U) {
        *result = INT_MIN;
    } else {
        *result = -(int)number;
    }
    return STATUS_OK;
}

static Status roman_digit(char c, int *result) {
    if (result == NULL) {
        return STATUS_ARGUMENT;
    }
    const char *digits = "IVXLCDM";
    const int values[] = {1, 5, 10, 50, 100, 500, 1000};
    const char *found = strchr(digits, c);
    if (c == '\0' || found == NULL) {
        return STATUS_FORMAT;
    }
    *result = values[found - digits];
    return STATUS_OK;
}

static Status parse_roman(const char *text, int *result) {
    if (text == NULL || result == NULL || text[0] == '\0') {
        return STATUS_FORMAT;
    }
    int negative = text[0] == '-';
    if (text[0] == '-' || text[0] == '+') {
        ++text;
    }
    if (text[0] == '\0') {
        return STATUS_FORMAT;
    }
    if (strcmp(text, "N") == 0) {
        *result = 0;
        return STATUS_OK;
    }
    long long value = 0;
    for (size_t i = 0; text[i] != '\0'; ++i) {
        int current = 0;
        int next = 0;
        Status status = roman_digit(text[i], &current);
        if (status == STATUS_OK && text[i + 1U] != '\0') {
            status = roman_digit(text[i + 1U], &next);
        }
        if (status != STATUS_OK) {
            return status;
        }
        if (current < next) {
            value -= current;
        } else {
            value += current;
        }
        if (value > (long long)INT_MAX + 1LL || value < -(long long)INT_MAX) {
            return STATUS_RANGE;
        }
    }
    if (negative) {
        value = -value;
    }
    if (value < INT_MIN || value > INT_MAX) {
        return STATUS_RANGE;
    }
    *result = (int)value;
    return STATUS_OK;
}

static Status parse_zeckendorf(const char *text, unsigned int *result) {
    if (text == NULL || result == NULL) {
        return STATUS_ARGUMENT;
    }
    size_t length = strlen(text);
    if (length < 2U || text[length - 1U] != '1') {
        return STATUS_FORMAT;
    }
    unsigned int fibonacci[sizeof(unsigned int) * CHAR_BIT * 2U];
    size_t count = 2U;
    fibonacci[0] = 1U;
    fibonacci[1] = 2U;
    while (fibonacci[count - 1U] <= UINT_MAX - fibonacci[count - 2U]) {
        fibonacci[count] = fibonacci[count - 1U] + fibonacci[count - 2U];
        ++count;
    }
    if (length - 1U > count) {
        return STATUS_RANGE;
    }
    unsigned int number = 0;
    for (size_t i = 0; i + 1U < length; ++i) {
        if (text[i] != '0' && text[i] != '1') {
            return STATUS_FORMAT;
        }
        if (text[i] == '1') {
            if (i != 0 && text[i - 1U] == '1') {
                return STATUS_FORMAT;
            }
            if (number > UINT_MAX - fibonacci[i]) {
                return STATUS_RANGE;
            }
            number += fibonacci[i];
        }
    }
    if (number != 0 && text[length - 2U] != '1') {
        return STATUS_FORMAT;
    }
    *result = number;
    return STATUS_OK;
}

static Status scan_custom(Input *input, const char *flag, va_list *args, Outcome *outcome) {
    if (input == NULL || flag == NULL || args == NULL || outcome == NULL) {
        return STATUS_ARGUMENT;
    }
    *outcome = (Outcome){0, 0, 0};
    int *signed_target = NULL;
    unsigned int *unsigned_target = NULL;
    if (flag[0] == 'Z') {
        unsigned_target = va_arg(*args, unsigned int *);
    } else {
        signed_target = va_arg(*args, int *);
    }
    if (signed_target == NULL && unsigned_target == NULL) {
        return STATUS_ARGUMENT;
    }
    int base = 10;
    char kind = flag[0];
    if (flag[0] == 'C') {
        base = va_arg(*args, int);
        if (base < 2 || base > 36) {
            base = 10;
        }
        if (flag[1] == 'v') {
            kind = 'c';
        }
    }
    char *text = NULL;
    Status status = read_custom_token(input, kind, base, &text, &outcome->input_failure);
    if (status != STATUS_OK) {
        return status;
    }
    int signed_value = 0;
    unsigned int unsigned_value = 0;
    if (kind == 'R') {
        status = parse_roman(text, &signed_value);
    } else if (kind == 'Z') {
        status = parse_zeckendorf(text, &unsigned_value);
    } else {
        status = parse_base(text, base, kind == 'C', &signed_value);
    }
    free(text);
    if (status == STATUS_OK) {
        if (unsigned_target != NULL) {
            *unsigned_target = unsigned_value;
        } else {
            *signed_target = signed_value;
        }
        outcome->matched = 1;
        outcome->assigned = 1;
    }
    return STATUS_OK;
}

static Status scan_input(Input *input, const char *format, va_list *args, int *result) {
    if (input == NULL || format == NULL || args == NULL || result == NULL) {
        return STATUS_ARGUMENT;
    }
    int assigned = 0;
    int input_failure = 0;
    size_t i = 0;
    Status status = STATUS_OK;
    while (format[i] != '\0' && status == STATUS_OK) {
        if (isspace((unsigned char)format[i])) {
            while (isspace((unsigned char)format[i])) {
                ++i;
            }
            status = skip_space(input);
            continue;
        }
        if (format[i] != '%' || format[i + 1U] == '%') {
            int expected = (unsigned char)format[i];
            int c;
            status = input_get(input, &c);
            if (status != STATUS_OK) {
                break;
            }
            if (c != expected) {
                if (c == EOF) {
                    input_failure = 1;
                } else {
                    status = input_unget(input, c);
                }
                break;
            }
            if (format[i] == '%') {
                i += 2U;
            } else {
                ++i;
            }
            continue;
        }
        char first = format[i + 1U];
        char second = '\0';
        if (first != '\0') {
            second = format[i + 2U];
        }
        int custom = (first == 'R' && second == 'o') ||
                     (first == 'Z' && second == 'r') ||
                     (first == 'C' && (second == 'v' || second == 'V'));
        Outcome outcome;
        if (custom) {
            status = scan_custom(input, format + i + 1U, args, &outcome);
            i += 3U;
        } else {
            Spec spec;
            status = parse_spec(format + i, &spec);
            if (status != STATUS_OK) {
                break;
            }
            if (spec.conversion == 'n') {
                status = store_count(args, spec.modifier, input->consumed);
                i += spec.length;
                continue;
            }
            status = scan_standard(input, format + i, &spec, args, &outcome);
            i += spec.length;
        }
        if (status != STATUS_OK) {
            break;
        }
        if (!outcome.matched) {
            input_failure = outcome.input_failure;
            break;
        }
        if (assigned > INT_MAX - outcome.assigned) {
            status = STATUS_RANGE;
            break;
        }
        assigned += outcome.assigned;
    }
    if (input_failure && assigned == 0) {
        *result = EOF;
    } else {
        *result = assigned;
    }
    return status;
}

int overfscanf(FILE *stream, const char *format, ...) {
    if (stream == NULL || format == NULL) {
        return EOF;
    }
    Input input = {stream, NULL, 0, 0};
    va_list args;
    va_start(args, format);
    int result = 0;
    Status status = scan_input(&input, format, &args, &result);
    va_end(args);
    if (status != STATUS_OK) {
        return EOF;
    }
    return result;
}

int oversscanf(const char *text, const char *format, ...) {
    if (text == NULL || format == NULL) {
        return EOF;
    }
    Input input = {NULL, text, 0, 0};
    va_list args;
    va_start(args, format);
    int result = 0;
    Status status = scan_input(&input, format, &args, &result);
    va_end(args);
    if (status != STATUS_OK) {
        return EOF;
    }
    return result;
}

int main(int argc, char *argv[]) {
    if (argc != 1) {
        fprintf(stderr, "Использование: %s\n", argv[0]);
        return EXIT_FAILURE;
    }
    int roman = 0;
    int lower = 0;
    int upper = 0;
    int ordinary = 0;
    unsigned int zeckendorf = 0;
    int count = oversscanf("MMXXVI 0011 ff FF 42", "%Ro %Zr %Cv %CV %d",
                          &roman, &zeckendorf, &lower, 16, &upper, 16, &ordinary);
    if (count != 5 || printf("string: %d %u %d %d %d\n", roman, zeckendorf, lower, upper, ordinary) < 0) {
        return EXIT_FAILURE;
    }
    FILE *stream = tmpfile();
    if (stream == NULL) {
        fputs("Ошибка открытия временного файла.\n", stderr);
        return EXIT_FAILURE;
    }
    int success = fputs("XLII 011 -101 7B 2.5", stream) != EOF;
    double real = 0.0;
    if (success && fseek(stream, 0L, SEEK_SET) != 0) {
        success = 0;
    }
    if (success) {
        count = overfscanf(stream, "%Ro %Zr %Cv %CV %lf", &roman, &zeckendorf, &lower, 2, &upper, 16, &real);
        success = count == 5;
    }
    if (fclose(stream) != 0) {
        success = 0;
    }
    if (success && printf("file: %d %u %d %d %.3f\n", roman, zeckendorf, lower, upper, real) < 0) {
        success = 0;
    }
    if (fflush(stdout) == EOF) {
        success = 0;
    }
    if (!success) {
        fputs("Ошибка чтения или вывода.\n", stderr);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
