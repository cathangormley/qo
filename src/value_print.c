#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <limits.h>
#include <math.h>
#include <time.h>
#include "evaluator_internal.h"
#include "internal.h"

static void qo_print_internal(Qo q, int depth);

/* Format an int64 number of nanoseconds since unix epoch as YYYY.MM.DDTHH:MM:SS.NNNNNNNNN. */
static void format_timestamp_ns(int64_t ns, char *out, size_t outlen) {
    int64_t secs = ns / 1000000000LL;
    int64_t nanos = ns % 1000000000LL;
    if (nanos < 0) { nanos += 1000000000LL; secs -= 1; }
    time_t t = (time_t)secs;
    struct tm tm;
    gmtime_r(&t, &tm);
    snprintf(out, outlen, "%04d.%02d.%02dT%02d:%02d:%02d.%09ld",
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
             tm.tm_hour, tm.tm_min, tm.tm_sec, (long)nanos);
}

static void format_timespan_ns(int64_t ns, char *out, size_t outlen) {
    int neg = (ns < 0);
    if (neg) ns = -ns;
    int64_t days = ns / 86400000000000LL;
    int64_t rem = ns % 86400000000000LL;
    int64_t hours = rem / 3600000000000LL;
    rem %= 3600000000000LL;
    int64_t minutes = rem / 60000000000LL;
    rem %= 60000000000LL;
    int64_t seconds = rem / 1000000000LL;
    int64_t nanos = rem % 1000000000LL;
    if (neg)
        snprintf(out, outlen, "-%ldT%02ld:%02ld:%02ld.%09ld", days, hours, minutes, seconds, nanos);
    else
        snprintf(out, outlen, "%ldT%02ld:%02ld:%02ld.%09ld", days, hours, minutes, seconds, nanos);
}

static char *qo_print_buffer;
static size_t qo_print_buffer_len;
static size_t qo_print_buffer_cap;
static int qo_print_buffering;

/* Writes text either directly to stdout or into the temporary print buffer. */
static void qo_print_emit(const char *text, size_t len) {
    if (len == 0) return;
    if (!qo_print_buffering) {
        fwrite(text, 1, len, stdout);
        return;
    }
    if (qo_print_buffer_len + len + 1 > qo_print_buffer_cap) {
        size_t new_cap = qo_print_buffer_cap == 0 ? 128 : qo_print_buffer_cap;
        while (qo_print_buffer_len + len + 1 > new_cap) new_cap *= 2;
        qo_print_buffer = xrealloc(qo_print_buffer, new_cap);
        qo_print_buffer_cap = new_cap;
    }
    memcpy(qo_print_buffer + qo_print_buffer_len, text, len);
    qo_print_buffer_len += len;
    qo_print_buffer[qo_print_buffer_len] = '\0';
}

/* printf-style helper that routes formatted text through qo_print_emit(). */
static void qo_printf(const char *fmt, ...) {
    va_list ap;
    va_list ap_copy;
    int needed;
    char *buf;

    va_start(ap, fmt);
    va_copy(ap_copy, ap);
    needed = vsnprintf(NULL, 0, fmt, ap_copy);
    va_end(ap_copy);
    if (needed <= 0) {
        va_end(ap);
        return;
    }

    buf = xmalloc((size_t)needed + 1);
    vsnprintf(buf, (size_t)needed + 1, fmt, ap);
    va_end(ap);

    qo_print_emit(buf, (size_t)needed);
    free(buf);
}

/* Renders list values, with top-level row-style formatting for mixed vector rows. */
static void qo_print_list_style(Qo q, int depth) {
    int64_t n = QO_COUNT(q);

    if (depth == 0) {
        for (int64_t i = 0; i < n; i++) {
            if (i > 0) qo_printf("\n");
            qo_print_internal(QO_LIST_DATA(q)[i], depth + 1);
        }
        return;
    }

    qo_printf("(");
    for (int64_t i = 0; i < n; i++) {
        if (i > 0) qo_printf(";");
        qo_print_internal(QO_LIST_DATA(q)[i], depth + 1);
    }
    qo_printf(")");
}

/* Renders function values from source when available, otherwise from body expressions. */
static void qo_print_function_style(Qo q, int depth) {
    int64_t sl = QO_FN_SL(q);
    if (sl > 0) {
        qo_printf("%s", QO_FN_SOURCE(q));
        return;
    }
    qo_printf("{");
    for (int64_t i = 0; i < QO_FN_BC(q); i++) {
        if (i > 0) qo_printf("; ");
        qo_print_internal(QO_FN_BODY(q)[i], depth + 1);
    }
    qo_printf("}");
}

/* Renders projection values as function[arg0;arg1;...]. */
static void qo_print_projection_style(Qo q, int depth) {
    qo_print_internal(QO_PROJ_FUNC(q), depth + 1);
    qo_printf("[");
    for (int64_t i = 0; i < QO_PROJ_AC(q); i++) {
        if (i > 0) qo_printf(";");
        if (QO_PROJ_ARGS(q)[i] != NULL && QO_TYPE(QO_PROJ_ARGS(q)[i]) != QO_PROJECTOR) {
            qo_print_internal(QO_PROJ_ARGS(q)[i], depth + 1);
        }
    }
    qo_printf("]");
}

/* Recursive dispatcher that renders every Qo runtime type. */
static void qo_print_internal(Qo q, int depth) {
    if (q == NULL) return;

    switch (QO_TYPE(q)) {
        case QO_SHORT: {
            int16_t sv = QO_SHORT_VAL(q);
            if (sv == QO_SHORT_NULL) qo_printf("0Nh");
            else qo_printf("%ldh", (long)sv);
            break;
        }
        case QO_INT: {
            int32_t iv = QO_INT_VAL(q);
            if (iv == QO_INT_NULL) qo_printf("0Ni");
            else if (iv == QO_INT_INF) qo_printf("0Wi");
            else if (iv == QO_INT_NEGINF) qo_printf("-0Wi");
            else qo_printf("%ldi", (long)iv);
            break;
        }
        case QO_LONG: {
            int64_t lv = QO_LONG_VAL(q);
            if (lv == QO_LONG_NULL) qo_printf("0N");
            else if (lv == QO_LONG_INF) qo_printf("0W");
            else if (lv == QO_LONG_NEGINF) qo_printf("-0W");
            else qo_printf("%ld", lv);
            break;
        }
        case QO_TIMESTAMP: {
            int64_t ns = qo_timestamp(q);
            if (ns == QO_LONG_NULL) { qo_printf("0Np"); break; }
            char buf[64];
            format_timestamp_ns(ns, buf, sizeof buf);
            qo_printf("%s", buf);
            break;
        }
        case QO_TIMESPAN: {
            int64_t ns = qo_timespan(q);
            if (ns == QO_LONG_NULL) { qo_printf("0N"); break; }
            char buf[64];
            format_timespan_ns(ns, buf, sizeof buf);
            qo_printf("%s", buf);
            break;
        }
        case QO_FLOAT: {
            double fv = QO_FLOAT_VAL(q);
            if (isnan(fv)) qo_printf("0Nf");
            else if (isinf(fv)) qo_printf(fv > 0 ? "0Wf" : "-0Wf");
            else qo_printf("%gf", fv);
            break;
        }
        case QO_CHAR: {
            char c = QO_CHAR_VAL(q);
            if (c == 0) qo_printf("'\\0'");
            else qo_printf("'%c'", c);
            break;
        }
        case QO_BOOL:
            qo_printf("%ldb", (long)QO_BOOL_VAL(q));
            break;
        case QO_BYTE:
            qo_printf("0x%02x", (unsigned int)QO_BYTE_VAL(q));
            break;
        case QO_PROJECTOR:
            qo_printf("P");
            break;
        case QO_ADVERB: {
            uint8_t kind = QO_ADVERB_KIND(q);
            if (kind == QO_ADVERB_EACH) qo_printf("'");
            else qo_printf("<adverb %d>", (int)kind);
            break;
        }
        case QO_SYMBOL:
            qo_printf("`%s", qo_symbol_name(q));
            break;
        case QO_OPERATOR: {
            const char *name = token_type_to_operator((TokenType)QO_OPERATOR_OP(q));
            if (name) qo_printf("%s", name);
            else      qo_printf("<operator %d>", (int)QO_OPERATOR_OP(q));
            break;
        }
        case QO_BUILTIN: {
            const char *name = builtin_id_to_name(QO_BUILTIN_ID(q));
            if (name) qo_printf("%s", name);
            else      qo_printf("<builtin %d>", (int)QO_BUILTIN_ID(q));
            break;
        }
        case QO_CHAR_VEC: {
            int64_t n = QO_COUNT(q);
            qo_printf("\"");
            for (int64_t i = 0; i < n; i++) qo_printf("%c", QO_CHAR_DATA(q)[i]);
            qo_printf("\"");
            break;
        }
        case QO_BOOL_VEC: {
            int64_t n = QO_COUNT(q);
            for (int64_t i = 0; i < n; i++) qo_printf("%ld", (long)QO_BOOL_DATA(q)[i]);
            qo_printf("b");
            break;
        }
        case QO_BYTE_VEC: {
            int64_t n = QO_COUNT(q);
            qo_printf("0x");
            for (int64_t i = 0; i < n; i++) qo_printf("%02x", (unsigned int)QO_BYTE_DATA(q)[i]);
            break;
        }
        case QO_SHORT_VEC: {
            int64_t n = QO_COUNT(q);
            if (n == 0) {
                qo_printf("[]");
                break;
            }
            for (int64_t i = 0; i < n; i++) {
                if (i > 0) qo_printf(" ");
                int16_t v = QO_SHORT_DATA(q)[i];
                if (v == QO_SHORT_NULL) qo_printf("0N");
                else qo_printf("%ld", (long)v);
            }
            qo_printf("h");
            break;
        }
        case QO_INT_VEC: {
            int64_t n = QO_COUNT(q);
            if (n == 0) {
                qo_printf("[]");
                break;
            }
            for (int64_t i = 0; i < n; i++) {
                if (i > 0) qo_printf(" ");
                int32_t v = QO_INT_DATA(q)[i];
                if (v == QO_INT_NULL) qo_printf("0N");
                else if (v == QO_INT_INF) qo_printf("0W");
                else if (v == QO_INT_NEGINF) qo_printf("-0W");
                else qo_printf("%ld", (long)v);
            }
            qo_printf("i");
            break;
        }
        case QO_LONG_VEC: {
            int64_t n = QO_COUNT(q);
            if (n == 0) {
                qo_printf("[]");
                break;
            }
            for (int64_t i = 0; i < n; i++) {
                if (i > 0) qo_printf(" ");
                int64_t v = QO_LONG_DATA(q)[i];
                if (v == QO_LONG_NULL) qo_printf("0N");
                else if (v == QO_LONG_INF) qo_printf("0W");
                else if (v == QO_LONG_NEGINF) qo_printf("-0W");
                else qo_printf("%ld", v);
            }
            break;
        }
        case QO_TIMESTAMP_VEC: {
            int64_t n = QO_COUNT(q);
            if (n == 0) { qo_printf("[]"); break; }
            for (int64_t i = 0; i < n; i++) {
                if (i > 0) qo_printf(" ");
                int64_t v = QO_LONG_DATA(q)[i];
                if (v == QO_LONG_NULL) { qo_printf("0N"); continue; }
                char buf[64];
                format_timestamp_ns(v, buf, sizeof buf);
                qo_printf("%s", buf);
            }
            break;
        }
        case QO_TIMESPAN_VEC: {
            int64_t n = QO_COUNT(q);
            if (n == 0) { qo_printf("[]"); break; }
            for (int64_t i = 0; i < n; i++) {
                if (i > 0) qo_printf(" ");
                int64_t v = QO_LONG_DATA(q)[i];
                if (v == QO_LONG_NULL) { qo_printf("0N"); continue; }
                char buf[64];
                format_timespan_ns(v, buf, sizeof buf);
                qo_printf("%s", buf);
            }
            break;
        }
        case QO_FLOAT_VEC: {
            int64_t n = QO_COUNT(q);
            if (n == 0) {
                qo_printf("[]");
                break;
            }
            for (int64_t i = 0; i < n; i++) {
                if (i > 0) qo_printf(" ");
                double v = QO_FLOAT_DATA(q)[i];
                if (isnan(v)) qo_printf("0N");
                else if (isinf(v)) qo_printf(v > 0 ? "0W" : "-0W");
                else qo_printf("%g", v);
            }
            qo_printf("f");
            break;
        }
        case QO_SYM_VEC: {
            int64_t n = QO_COUNT(q);
            if (n == 0) { qo_printf("[]"); break; }
            for (int64_t i = 0; i < n; i++) {
                qo_printf("`%s", qo_symbol_name(QO_LIST_DATA(q)[i]));
            }
            break;
        }
        case QO_LIST:
            qo_print_list_style(q, depth);
            break;
        case QO_FUNCTION:
            qo_print_function_style(q, depth);
            break;
        case QO_PROJECTION:
            qo_print_projection_style(q, depth);
            break;
        case QO_EACHED:
            qo_print_internal(QO_EACHED_FUNC(q), depth);
            qo_printf("'");
            break;
        case QO_DICT: {
            int64_t n = QO_DICT_COUNT(q);
            for (int64_t i = 0; i < n; i++) {
                if (i > 0) qo_printf("\n");
                qo_print_internal(QO_DICT_KEYS(q)[i], depth + 1);
                qo_printf(" | ");
                qo_print_internal(QO_DICT_VALS(q)[i], depth + 1);
            }
            break;
        }
        default:
            break;
    }
}

/* Public entry point for full value rendering without output limits. */
void qo_print(Qo q) {
    qo_print_internal(q, 0);
}

/* Emits one line with max width handling and '..' truncation marker when needed. */
static void qo_print_emit_trimmed_line(const char *line, size_t len, int max_chars) {
    if (max_chars <= 0) return;
    if ((int)len <= max_chars) {
        if (len > 0) fwrite(line, 1, len, stdout);
        return;
    }
    if (max_chars <= 2) {
        for (int i = 0; i < max_chars; i++) fputc('.', stdout);
        return;
    }
    fwrite(line, 1, (size_t)(max_chars - 2), stdout);
    fwrite("..", 1, 2, stdout);
}

/* Public entry point that renders with maximum line and per-line character limits. */
void qo_print_with_limits(Qo q, int max_lines, int max_chars_per_line) {
    char *prev_buffer = qo_print_buffer;
    size_t prev_len = qo_print_buffer_len;
    size_t prev_cap = qo_print_buffer_cap;
    int prev_buffering = qo_print_buffering;
    int line_count = 0;
    size_t start = 0;

    if (max_lines < 0 || max_chars_per_line < 0) {
        qo_print(q);
        return;
    }

    qo_print_buffering = 1;
    qo_print_buffer = NULL;
    qo_print_buffer_len = 0;
    qo_print_buffer_cap = 0;
    qo_print_internal(q, 0);

    while (start < qo_print_buffer_len && line_count < max_lines) {
        size_t end = start;
        int has_more_lines;

        while (end < qo_print_buffer_len && qo_print_buffer[end] != '\n') end++;
        has_more_lines = (end < qo_print_buffer_len);
        line_count += 1;

        if (line_count == max_lines && (has_more_lines || end < qo_print_buffer_len)) {
            qo_print_emit_trimmed_line("..", 2, max_chars_per_line);
            break;
        }

        qo_print_emit_trimmed_line(qo_print_buffer + start,
                                   end - start,
                                   max_chars_per_line);
        if (has_more_lines && line_count < max_lines) fputc('\n', stdout);
        start = end + (has_more_lines ? 1 : 0);
    }

    free(qo_print_buffer);
    qo_print_buffer = prev_buffer;
    qo_print_buffer_len = prev_len;
    qo_print_buffer_cap = prev_cap;
    qo_print_buffering = prev_buffering;
}

