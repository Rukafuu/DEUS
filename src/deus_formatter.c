#include "deus_formatter.h"

#include "deus_source_ast.h"
#include "deus_source_parser.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *data;
    size_t length;
    size_t capacity;
} Buffer;

typedef struct {
    unsigned delimiters;
    int quote;
    int escaped;
} DelimiterState;

static int reserve(Buffer *buffer, size_t extra) {
    size_t required, capacity; char *next;
    if (extra > SIZE_MAX - buffer->length - 1u) return 0;
    required = buffer->length + extra + 1u;
    if (required <= buffer->capacity) return 1;
    capacity = buffer->capacity ? buffer->capacity : 256u;
    while (capacity < required) {
        if (capacity > SIZE_MAX / 2u) { capacity = required; break; }
        capacity *= 2u;
    }
    next = (char *)realloc(buffer->data, capacity);
    if (!next) return 0;
    buffer->data = next; buffer->capacity = capacity; return 1;
}

static int append(Buffer *buffer, const char *text, size_t length) {
    if (!reserve(buffer, length)) return 0;
    if (length) memcpy(buffer->data + buffer->length, text, length);
    buffer->length += length; buffer->data[buffer->length] = '\0'; return 1;
}

static int spaces(Buffer *buffer, unsigned count) {
    if (!reserve(buffer, count)) return 0;
    memset(buffer->data + buffer->length, ' ', count);
    buffer->length += count; buffer->data[buffer->length] = '\0'; return 1;
}

static size_t physical_end(const char *source, size_t end, size_t start) {
    size_t cursor = start;
    while (cursor < end && source[cursor] != '\r' && source[cursor] != '\n') cursor++;
    return cursor;
}

static size_t next_line(const char *source, size_t end, size_t cursor) {
    if (cursor < end && source[cursor] == '\r') cursor++;
    if (cursor < end && source[cursor] == '\n') cursor++;
    return cursor;
}

static void trim_line(const char *source, size_t *start, size_t *end) {
    while (*start < *end && (source[*start] == ' ' || source[*start] == '\t')) (*start)++;
    while (*end > *start && (source[*end - 1u] == ' ' || source[*end - 1u] == '\t')) (*end)--;
}

static int closing_delimiter(char ch) {
    return ch == '}' || ch == ']' || ch == ')';
}

static void scan_delimiters(DelimiterState *state, const char *text, size_t length) {
    size_t index; int comment = 0;
    for (index = 0u; index < length; index++) {
        unsigned char ch = (unsigned char)text[index];
        if (comment) break;
        if (state->quote) {
            if (state->escaped) state->escaped = 0;
            else if (ch == '\\') state->escaped = 1;
            else if (ch == (unsigned char)state->quote) state->quote = 0;
        } else if (ch == '"' || ch == '\'') state->quote = ch;
        else if (ch == '#' || (ch == '/' && index + 1u < length && text[index + 1u] == '/')) comment = 1;
        else if (ch == '{' || ch == '[' || ch == '(') state->delimiters++;
        else if (closing_delimiter((char)ch) && state->delimiters) state->delimiters--;
    }
}

static int append_physical(Buffer *buffer, const char *source,
                           size_t start, size_t end, unsigned semantic_depth,
                           DelimiterState *state, int continuation) {
    size_t content_start = start, content_end = end; unsigned indent_depth;
    trim_line(source, &content_start, &content_end);
    if (content_start == content_end) return append(buffer, "\n", 1u);
    indent_depth = semantic_depth + (continuation ? state->delimiters : 0u);
    if (continuation && closing_delimiter(source[content_start]) && indent_depth > semantic_depth)
        indent_depth--;
    if (!spaces(buffer, indent_depth * 4u) ||
        !append(buffer, source + content_start, content_end - content_start) ||
        !append(buffer, "\n", 1u)) return 0;
    scan_delimiters(state, source + content_start, content_end - content_start);
    return 1;
}

static int append_logical_line(Buffer *buffer, const DeusSourceAst *ast,
                               const DeusSourceLogicalLine *line) {
    size_t cursor = line->span.start.offset, end = line->span.end.offset;
    DelimiterState state = {0u, 0, 0}; int continuation = 0;
    if (line->kind == DEUS_SOURCE_LINE_BLANK) return append(buffer, "\n", 1u);
    while (cursor <= end) {
        size_t physical = physical_end(ast->source, end, cursor);
        if (!append_physical(buffer, ast->source, cursor, physical,
                             line->depth, &state, continuation)) return 0;
        if (physical == end) break;
        cursor = next_line(ast->source, end, physical); continuation = 1;
    }
    return 1;
}

static int format_modern(const char *source, size_t length, Buffer *buffer,
                         DeusDiagnostic *diagnostic) {
    DeusSourceAst ast; const DeusSourceLogicalLine *lines; size_t count, index;
    if (!deus_source_parse_modern(source, length, &ast, diagnostic)) return 0;
    lines = deus_source_ast_lines(&ast, &count);
    for (index = 0u; index < count; index++) {
        if (!append_logical_line(buffer, &ast, &lines[index])) {
            diagnostic->line = lines[index].span.start.line;
            diagnostic->column = 1u;
            memcpy(diagnostic->message, "out of memory", sizeof("out of memory"));
            deus_source_ast_free(&ast); return 0;
        }
    }
    deus_source_ast_free(&ast); return 1;
}

static int format_legacy(const char *source, size_t length, Buffer *buffer,
                         DeusDiagnostic *diagnostic) {
    size_t cursor = 0u; DelimiterState state = {0u, 0, 0};
    while (cursor < length) {
        size_t end = physical_end(source, length, cursor), start = cursor, trim = end;
        unsigned indent_depth = state.delimiters;
        trim_line(source, &start, &trim);
        if (start < trim && closing_delimiter(source[start]) && indent_depth) indent_depth--;
        if (start < trim && (!spaces(buffer, indent_depth * 4u) ||
            !append(buffer, source + start, trim - start))) goto memory_failed;
        if (!append(buffer, "\n", 1u)) goto memory_failed;
        if (start < trim) scan_delimiters(&state, source + start, trim - start);
        cursor = next_line(source, length, end);
    }
    return 1;
memory_failed:
    diagnostic->line = 1u; diagnostic->column = 1u;
    memcpy(diagnostic->message, "out of memory", sizeof("out of memory")); return 0;
}

static int canonical_final_newline(Buffer *buffer) {
    while (buffer->length && buffer->data[buffer->length - 1u] == '\n') buffer->length--;
    if (!reserve(buffer, 1u)) return 0;
    buffer->data[buffer->length++] = '\n'; buffer->data[buffer->length] = '\0';
    return 1;
}

int deus_format_source(const char *source, size_t length,
                       char **output, size_t *output_length,
                       DeusDiagnostic *diagnostic) {
    Buffer buffer = {0}; int ok;
    if (!output || !output_length || !diagnostic || (!source && length)) return 0;
    *output = NULL; *output_length = 0u; memset(diagnostic, 0, sizeof(*diagnostic));
    ok = deus_source_is_modern(source, length)
        ? format_modern(source, length, &buffer, diagnostic)
        : format_legacy(source, length, &buffer, diagnostic);
    if (!ok) { free(buffer.data); return 0; }
    if (!canonical_final_newline(&buffer)) { free(buffer.data); return 0; }
    if (!buffer.data) {
        buffer.data = (char *)malloc(2u);
        if (!buffer.data) return 0;
        buffer.data[0] = '\n'; buffer.data[1] = '\0'; buffer.length = 1u;
    }
    *output = buffer.data; *output_length = buffer.length; return 1;
}
