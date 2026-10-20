/*
    Quorum native runtime (proof of concept) for the LLVM backend.

    Text values are immutable, NUL terminated UTF-8 strings. Memory comes
    from the Boehm collector when compiled with -DQUORUM_USE_GC (link -lgc);
    otherwise it is calloc'd and never freed, which is fine for short programs.

    Build:  clang -O2 Program.ll quorum_runtime.c -o program
    or:     clang -O2 -DQUORUM_USE_GC Program.ll quorum_runtime.c -lgc -o program
*/
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#ifdef QUORUM_USE_GC
#include <gc.h>
#define QUORUM_RAW_ALLOC(n) GC_MALLOC(n)
#define QUORUM_RAW_ALLOC_ATOMIC(n) GC_MALLOC_ATOMIC(n)
#else
#define QUORUM_RAW_ALLOC(n) calloc(1, (n))
#define QUORUM_RAW_ALLOC_ATOMIC(n) calloc(1, (n))
#endif

static int quorum_argc = 0;
static char **quorum_argv = NULL;

static void quorum_fatal(const char *error_type, const char *message) {
    fflush(stdout);
    fprintf(stderr, "%s: %s\n", error_type, message);
    exit(1);
}

void quorum_runtime_init(int32_t argc, char **argv) {
#ifdef QUORUM_USE_GC
    GC_INIT();
#endif
    quorum_argc = argc;
    quorum_argv = argv;
}

void *quorum_alloc(int64_t size) {
    void *memory = QUORUM_RAW_ALLOC((size_t)(size > 0 ? size : 1));
    if (memory == NULL) {
        quorum_fatal("OutOfMemoryError", "the program ran out of memory");
    }
    return memory;
}

static char *quorum_new_text(size_t length) {
    char *result = QUORUM_RAW_ALLOC_ATOMIC(length + 1);
    if (result == NULL) {
        quorum_fatal("OutOfMemoryError", "the program ran out of memory");
    }
    result[length] = '\0';
    return result;
}

static char *quorum_copy_text(const char *value) {
    size_t length = strlen(value);
    char *result = quorum_new_text(length);
    memcpy(result, value, length);
    return result;
}

static const char *quorum_display(const char *value) {
    return value == NULL ? "undefined" : value;
}

/* ---------------- statements ---------------- */

void quorum_output(const char *value) {
    fputs(quorum_display(value), stdout);
    fputc('\n', stdout);
}

void quorum_say(const char *value) {
    /* Speech is platform specific; print it so programs remain observable. */
    fflush(stdout);
    fprintf(stderr, "[say] %s\n", quorum_display(value));
}

char *quorum_input(const char *prompt) {
    if (prompt != NULL) {
        fputs(prompt, stdout);
        fflush(stdout);
    }
    size_t capacity = 128, length = 0;
    char *buffer = malloc(capacity);
    int c;
    while ((c = fgetc(stdin)) != EOF && c != '\n') {
        if (length + 1 >= capacity) {
            capacity *= 2;
            buffer = realloc(buffer, capacity);
        }
        buffer[length++] = (char)c;
    }
    if (length > 0 && buffer[length - 1] == '\r') {
        length--;
    }
    buffer[length] = '\0';
    char *result = quorum_copy_text(buffer);
    free(buffer);
    return result;
}

/* ---------------- text ---------------- */

char *quorum_text_concat(const char *left, const char *right) {
    left = quorum_display(left);
    right = quorum_display(right);
    size_t l = strlen(left), r = strlen(right);
    char *result = quorum_new_text(l + r);
    memcpy(result, left, l);
    memcpy(result + l, right, r);
    return result;
}

char *quorum_text_from_integer(int32_t value) {
    char buffer[16];
    snprintf(buffer, sizeof buffer, "%d", value);
    return quorum_copy_text(buffer);
}

char *quorum_text_from_boolean(int32_t value) {
    return quorum_copy_text(value ? "true" : "false");
}

/*
    Formats a number the way Java's Double.toString does (which is what the
    JVM backend prints): the shortest digits that round trip, plain decimal
    notation for 1e-3 <= |v| < 1e7, otherwise d.dddE<exp>. Always at least
    one digit after the decimal point.
*/
char *quorum_text_from_number(double value) {
    if (isnan(value)) return quorum_copy_text("NaN");
    if (isinf(value)) return quorum_copy_text(value > 0 ? "Infinity" : "-Infinity");
    if (value == 0.0) return quorum_copy_text(signbit(value) ? "-0.0" : "0.0");

    char scientific[40];
    int precision;
    for (precision = 0; precision < 17; precision++) {
        snprintf(scientific, sizeof scientific, "%.*e", precision, value);
        if (strtod(scientific, NULL) == value) break;
    }

    /* split "-d.ddde+XX" into sign, digits and exponent */
    int negative = scientific[0] == '-';
    const char *p = scientific + negative;
    char digits[24];
    int count = 0;
    while (*p && *p != 'e') {
        if (*p != '.') digits[count++] = *p;
        p++;
    }
    while (count > 1 && digits[count - 1] == '0') count--;
    digits[count] = '\0';
    int exponent = atoi(p + 1);

    char out[64];
    int n = 0;
    if (negative) out[n++] = '-';
    double magnitude = fabs(value);
    if (magnitude >= 1e-3 && magnitude < 1e7) {
        if (exponent >= 0) {
            for (int i = 0; i <= exponent; i++) out[n++] = i < count ? digits[i] : '0';
            out[n++] = '.';
            if (exponent + 1 < count) {
                for (int i = exponent + 1; i < count; i++) out[n++] = digits[i];
            } else {
                out[n++] = '0';
            }
        } else {
            out[n++] = '0';
            out[n++] = '.';
            for (int i = 0; i < -exponent - 1; i++) out[n++] = '0';
            for (int i = 0; i < count; i++) out[n++] = digits[i];
        }
        out[n] = '\0';
    } else {
        out[n++] = digits[0];
        out[n++] = '.';
        if (count > 1) {
            for (int i = 1; i < count; i++) out[n++] = digits[i];
        } else {
            out[n++] = '0';
        }
        snprintf(out + n, sizeof out - n, "E%d", exponent);
    }
    return quorum_copy_text(out);
}

int32_t quorum_text_to_integer(const char *value) {
    if (value == NULL) quorum_fatal("CastError", "cannot convert undefined to an integer");
    errno = 0;
    char *end = NULL;
    long result = strtol(value, &end, 10);
    if (end == value || *end != '\0' || errno == ERANGE || result < INT32_MIN || result > INT32_MAX) {
        char message[256];
        snprintf(message, sizeof message, "cannot convert \"%.200s\" to an integer", value);
        quorum_fatal("CastError", message);
    }
    return (int32_t)result;
}

double quorum_text_to_number(const char *value) {
    if (value == NULL) quorum_fatal("CastError", "cannot convert undefined to a number");
    char *end = NULL;
    double result = strtod(value, &end);
    while (end != NULL && (*end == ' ' || *end == '\t')) end++;
    if (end == value || *end != '\0') {
        char message[256];
        snprintf(message, sizeof message, "cannot convert \"%.200s\" to a number", value);
        quorum_fatal("CastError", message);
    }
    return result;
}

int32_t quorum_text_to_boolean(const char *value) {
    return value != NULL && strcasecmp(value, "true") == 0;
}

int32_t quorum_text_compare(const char *left, const char *right) {
    if (left == NULL || right == NULL) {
        quorum_fatal("UndefinedObjectError", "cannot compare undefined text");
    }
    int result = strcmp(left, right);
    return result < 0 ? -1 : (result > 0 ? 1 : 0);
}

int32_t quorum_text_equals(const char *left, const char *right) {
    if (left == NULL || right == NULL) return left == right;
    return strcmp(left, right) == 0;
}

/* ---------------- arithmetic (Java semantics) ---------------- */

int32_t quorum_integer_divide(int32_t left, int32_t right) {
    if (right == 0) quorum_fatal("DivideByZeroError", "division by zero");
    if (left == INT32_MIN && right == -1) return INT32_MIN;
    return left / right;
}

int32_t quorum_integer_modulus(int32_t left, int32_t right) {
    if (right == 0) quorum_fatal("DivideByZeroError", "modulus by zero");
    if (right == -1) return 0;
    return left % right;
}

/* ---------------- checks ---------------- */

void quorum_check_defined(const void *object, const char *action) {
    if (object == NULL) {
        char message[256];
        snprintf(message, sizeof message, "the action %.200s was called on an undefined object", quorum_display(action));
        quorum_fatal("UndefinedObjectError", message);
    }
}

void quorum_unsupported(const char *message) {
    fflush(stdout);
    fprintf(stderr, "%s\n", quorum_display(message));
    exit(70);
}
