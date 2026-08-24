#include "deus_layout.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail(DeusDiagnostic *diagnostic, unsigned line, unsigned column,
                const char *message) {
    diagnostic->line = line; diagnostic->column = column;
    snprintf(diagnostic->message, sizeof(diagnostic->message), "%s", message); return 0;
}

static int blank_or_comment(const char *line, size_t length) {
    size_t index = 0u;
    while (index < length && (line[index] == ' ' || line[index] == '\t')) index++;
    return index == length || line[index] == '#' ||
           (index + 1u < length && line[index] == '/' && line[index + 1u] == '/');
}

static int flow_header(const char *line, size_t length) {
    size_t index = 0u, name_start;
    while (length && (line[length - 1u] == ' ' || line[length - 1u] == '\t')) length--;
    if (length < 7u || memcmp(line, "flow", 4u) || line[4] != ' ') return 0;
    index = 5u; name_start = index;
    if (!((line[index] >= 'A' && line[index] <= 'Z') ||
          (line[index] >= 'a' && line[index] <= 'z') || line[index] == '_')) return 0;
    while (index < length && ((line[index] >= 'A' && line[index] <= 'Z') ||
           (line[index] >= 'a' && line[index] <= 'z') ||
           (line[index] >= '0' && line[index] <= '9') || line[index] == '_')) index++;
    return index > name_start && index + 1u == length && line[index] == ':';
}

int deus_layout_lower(const char *source, size_t length, char **output,
                      size_t *output_length, int *was_lowered,
                      DeusDiagnostic *diagnostic) {
    size_t cursor = 0u, header_start = 0u, header_end = 0u, used = 0u;
    unsigned line_number = 1u, header_line = 0u; char *lowered; int body_seen = 0;
    *output = NULL; *output_length = 0u; *was_lowered = 0;
    while (cursor < length) {
        size_t start = cursor, end;
        while (cursor < length && source[cursor] != '\n' && source[cursor] != '\r') cursor++;
        end = cursor;
        if (!blank_or_comment(source + start, end - start)) {
            if (!flow_header(source + start, end - start)) return 1;
            header_start = start; header_end = end; header_line = line_number; break;
        }
        if (cursor < length && source[cursor] == '\r') cursor++;
        if (cursor < length && source[cursor] == '\n') cursor++;
        line_number++;
    }
    if (!header_line) return 1;
    if (length > SIZE_MAX - 16u) return fail(diagnostic, header_line, 1u, "source is too large");
    lowered = (char *)malloc(length + 16u); if (!lowered) return fail(diagnostic, header_line, 1u, "out of memory");
    if (header_start) { memcpy(lowered, source, header_start); used = header_start; }
    memcpy(lowered + used, "genesis", 7u); used += 7u;
    cursor = header_end;
    if (cursor < length && source[cursor] == '\r') cursor++;
    if (cursor < length && source[cursor] == '\n') cursor++;
    lowered[used++] = '\n'; line_number = header_line + 1u;
    while (cursor < length) {
        size_t start = cursor, end, indent = 0u;
        while (cursor < length && source[cursor] != '\n' && source[cursor] != '\r') cursor++;
        end = cursor;
        while (start + indent < end && source[start + indent] == ' ') indent++;
        if (start + indent < end && source[start + indent] == '\t') {
            free(lowered); return fail(diagnostic, line_number, (unsigned)indent + 1u,
                                       "tabs are not allowed in flow indentation");
        }
        if (!blank_or_comment(source + start, end - start)) {
            if (indent < 4u) { free(lowered); return fail(diagnostic, line_number, 1u, "flow body must be indented by four spaces"); }
            if (indent % 4u) { free(lowered); return fail(diagnostic, line_number, (unsigned)indent + 1u, "flow indentation must use multiples of four spaces"); }
            body_seen = 1; start += 4u;
        } else if (indent >= 4u) start += 4u;
        if (end > start) { memcpy(lowered + used, source + start, end - start); used += end - start; }
        lowered[used++] = '\n';
        if (cursor < length && source[cursor] == '\r') cursor++;
        if (cursor < length && source[cursor] == '\n') cursor++;
        line_number++;
    }
    if (!body_seen) { free(lowered); return fail(diagnostic, header_line, 1u, "flow requires an indented body"); }
    memcpy(lowered + used, "halt\n", 5u); used += 5u; lowered[used] = '\0';
    *output = lowered; *output_length = used; *was_lowered = 1; return 1;
}
