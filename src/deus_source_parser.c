#include "deus_source_parser.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    DeusSourceAst *ast;
    DeusDiagnostic *diagnostic;
    size_t capacity;
} Parser;

static int fail(Parser *parser, unsigned line, unsigned column,
                const char *message) {
    parser->diagnostic->line = line;
    parser->diagnostic->column = column;
    snprintf(parser->diagnostic->message, sizeof(parser->diagnostic->message),
             "%s", message);
    return 0;
}

static DeusSourcePosition position_at(const char *source, size_t offset) {
    DeusSourcePosition position = {offset, 1u, 1u};
    size_t index;
    for (index = 0u; index < offset; index++) {
        if (source[index] == '\n') { position.line++; position.column = 1u; }
        else position.column++;
    }
    return position;
}

static DeusSourceSpan span_at(const char *source, size_t start, size_t end) {
    DeusSourceSpan span;
    span.start = position_at(source, start);
    span.end = position_at(source, end);
    return span;
}

static int reserve(void **items, size_t *capacity, size_t count,
                   size_t item_size) {
    void *next;
    size_t cap = *capacity ? *capacity : 8u;
    if (count <= *capacity) return 1;
    while (cap < count) {
        if (cap > SIZE_MAX / 2u) return 0;
        cap *= 2u;
    }
    if (cap > SIZE_MAX / item_size) return 0;
    next = realloc(*items, cap * item_size);
    if (!next) return 0;
    *items = next; *capacity = cap; return 1;
}

static size_t physical_end(const char *source, size_t length, size_t start) {
    size_t end = start;
    while (end < length && source[end] != '\r' && source[end] != '\n') end++;
    return end;
}

static size_t next_line(const char *source, size_t length, size_t end) {
    if (end < length && source[end] == '\r') end++;
    if (end < length && source[end] == '\n') end++;
    return end;
}

static int scan_lines(Parser *parser) {
    const char *source = parser->ast->source;
    size_t length = parser->ast->source_length, cursor = 0u, line_capacity = 0u;
    unsigned line_number = 1u;
    while (cursor < length) {
        size_t logical_start = cursor, first_end = physical_end(source, length, cursor);
        size_t indent = 0u, content, logical_end = first_end, scan;
        int braces = 0, brackets = 0, parentheses = 0, quote = 0, escaped = 0;
        DeusSourceLogicalLine line;
        while (logical_start + indent < first_end && source[logical_start + indent] == ' ') indent++;
        if (logical_start + indent < first_end && source[logical_start + indent] == '\t')
            return fail(parser, line_number, (unsigned)indent + 1u,
                        "tabs are not allowed in leading indentation");
        content = logical_start + indent;
        memset(&line, 0, sizeof(line));
        if (content == first_end) line.kind = DEUS_SOURCE_LINE_BLANK;
        else if (source[content] == '#' ||
                 (content + 1u < first_end && source[content] == '/' && source[content + 1u] == '/'))
            line.kind = DEUS_SOURCE_LINE_COMMENT;
        else line.kind = DEUS_SOURCE_LINE_CONTENT;

        if (line.kind == DEUS_SOURCE_LINE_CONTENT) {
            scan = content;
            for (;;) {
                size_t end = physical_end(source, length, scan);
                size_t index;
                for (index = scan; index < end; index++) {
                    unsigned char ch = (unsigned char)source[index];
                    if (quote) {
                        if (escaped) escaped = 0;
                        else if (ch == '\\') escaped = 1;
                        else if (ch == (unsigned char)quote) quote = 0;
                    } else if (ch == '"' || ch == '\'') quote = ch;
                    else if (ch == '#') break;
                    else if (ch == '/' && index + 1u < end && source[index + 1u] == '/') break;
                    else if (ch == '{') braces++;
                    else if (ch == '}') { if (!braces) return fail(parser, line_number, (unsigned)(index - scan) + 1u, "unmatched `}`"); braces--; }
                    else if (ch == '[') brackets++;
                    else if (ch == ']') { if (!brackets) return fail(parser, line_number, (unsigned)(index - scan) + 1u, "unmatched `]`"); brackets--; }
                    else if (ch == '(') parentheses++;
                    else if (ch == ')') { if (!parentheses) return fail(parser, line_number, (unsigned)(index - scan) + 1u, "unmatched `)`"); parentheses--; }
                }
                logical_end = end;
                if (!braces && !brackets && !parentheses) break;
                scan = next_line(source, length, end);
                if (scan >= length) return fail(parser, line_number, (unsigned)indent + 1u, "unterminated multiline literal");
                {
                    size_t continuation_end = physical_end(source, length, scan), leading = 0u;
                    while (scan + leading < continuation_end && source[scan + leading] == ' ') leading++;
                    if (scan + leading < continuation_end && source[scan + leading] == '\t')
                        return fail(parser, line_number + 1u, (unsigned)leading + 1u,
                                    "tabs are not allowed in leading indentation");
                }
                line_number++;
            }
        }
        if (indent % 4u && line.kind == DEUS_SOURCE_LINE_CONTENT)
            return fail(parser, line_number, (unsigned)indent + 1u,
                        "indentation must use multiples of four spaces");
        line.indent = (unsigned)indent;
        line.depth = (unsigned)(indent / 4u);
        line.owner = DEUS_SOURCE_OWNER_NONE;
        line.span = span_at(source, logical_start, logical_end);
        line.content_span = span_at(source, content, logical_end);
        if (!reserve((void **)&parser->ast->lines, &line_capacity,
                     parser->ast->line_count + 1u, sizeof(line)))
            return fail(parser, line_number, 1u, "out of memory");
        parser->ast->lines[parser->ast->line_count++] = line;
        cursor = next_line(source, length, logical_end);
        line_number++;
    }
    return 1;
}

static void trim_span(const char *source, size_t *start, size_t *end) {
    while (*start < *end && (source[*start] == ' ' || source[*start] == '\t')) (*start)++;
    while (*end > *start && (source[*end - 1u] == ' ' || source[*end - 1u] == '\t')) (*end)--;
}

static int identifier(const char *source, size_t start, size_t end) {
    size_t index = start;
    if (start == end || !((source[index] >= 'A' && source[index] <= 'Z') ||
        (source[index] >= 'a' && source[index] <= 'z') || source[index] == '_')) return 0;
    for (index++; index < end; index++) if (!((source[index] >= 'A' && source[index] <= 'Z') ||
        (source[index] >= 'a' && source[index] <= 'z') ||
        (source[index] >= '0' && source[index] <= '9') || source[index] == '_')) return 0;
    return 1;
}

int deus_source_is_modern(const char *source, size_t length) {
    size_t cursor = 0u;
    while (cursor < length) {
        size_t end = physical_end(source, length, cursor), index = cursor;
        while (index < end && (source[index] == ' ' || source[index] == '\t')) index++;
        if (index == end || source[index] == '#' ||
            (index + 1u < end && source[index] == '/' && source[index + 1u] == '/')) {
            cursor = next_line(source, length, end); continue;
        }
        return index == cursor && end - index >= 5u && !memcmp(source + index, "flow ", 5u);
    }
    return 0;
}

static int append_item(Parser *parser, DeusSourceFlowItem item, size_t *capacity) {
    if (!reserve((void **)&parser->ast->flow.items, capacity,
                 parser->ast->flow.item_count + 1u, sizeof(item)))
        return fail(parser, item.span.start.line, item.span.start.column, "out of memory");
    parser->ast->flow.items[parser->ast->flow.item_count++] = item;
    return 1;
}

static int parse_limit_entry(Parser *parser, DeusSourceLogicalLine *line,
                             DeusSourceLimitsBlock *block, size_t *capacity,
                             unsigned *seen) {
    const char *source = parser->ast->source;
    size_t start = line->content_span.start.offset, end = line->content_span.end.offset;
    size_t name_start, name_end, value_start, value_end, index;
    DeusSourceLimitEntry entry; unsigned bit; char number[32]; char *tail;
    trim_span(source, &start, &end); name_start = start;
    while (start < end && source[start] != ' ' && source[start] != '\t') start++;
    name_end = start; while (start < end && (source[start] == ' ' || source[start] == '\t')) start++;
    value_start = start; value_end = end; trim_span(source, &value_start, &value_end);
    if (name_end == name_start || value_start == value_end)
        return fail(parser, line->span.start.line, line->content_span.start.column,
                    "expected a limit name and unsigned integer");
    for (index = value_start; index < value_end; index++) if (source[index] < '0' || source[index] > '9')
        return fail(parser, line->span.start.line, (unsigned)(index - line->span.start.offset) + 1u,
                    "limit value must be an unsigned integer literal");
    memset(&entry, 0, sizeof(entry));
    if (name_end - name_start == 7u && !memcmp(source + name_start, "workers", 7u)) entry.kind = DEUS_SOURCE_LIMIT_WORKERS;
    else if (name_end - name_start == 5u && !memcmp(source + name_start, "retry", 5u)) entry.kind = DEUS_SOURCE_LIMIT_RETRY;
    else if (name_end - name_start == 7u && !memcmp(source + name_start, "backoff", 7u)) entry.kind = DEUS_SOURCE_LIMIT_BACKOFF;
    else if (name_end - name_start == 4u && !memcmp(source + name_start, "rate", 4u)) entry.kind = DEUS_SOURCE_LIMIT_RATE;
    else {
        char message[192];
        if (name_end - name_start == 8u && !memcmp(source + name_start, "workerss", 8u))
            snprintf(message, sizeof(message), "unknown limit `workerss`; did you mean `workers`?");
        else snprintf(message, sizeof(message), "unknown limit `%.*s`", (int)(name_end - name_start), source + name_start);
        return fail(parser, line->span.start.line, line->content_span.start.column, message);
    }
    bit = 1u << (unsigned)entry.kind;
    if (*seen & bit) {
        char message[192];
        snprintf(message, sizeof(message), "duplicate `%.*s` entry in `limits`", (int)(name_end - name_start), source + name_start);
        return fail(parser, line->span.start.line, line->content_span.start.column, message);
    }
    if (value_end - value_start >= sizeof(number))
        return fail(parser, line->span.start.line, line->content_span.start.column, "limit value exceeds U32");
    memcpy(number, source + value_start, value_end - value_start); number[value_end - value_start] = '\0'; errno = 0;
    entry.value = (uint32_t)strtoul(number, &tail, 10);
    if (errno == ERANGE || *tail || strtoul(number, NULL, 10) > UINT32_MAX)
        return fail(parser, line->span.start.line, line->content_span.start.column, "limit value exceeds U32");
    entry.span = line->span;
    entry.name_span = span_at(source, name_start, name_end);
    entry.value_span = span_at(source, value_start, value_end);
    if (!reserve((void **)&block->entries, capacity, block->entry_count + 1u, sizeof(entry)))
        return fail(parser, line->span.start.line, 1u, "out of memory");
    block->entries[block->entry_count++] = entry; *seen |= bit; return 1;
}

int deus_source_parse_modern(const char *source, size_t length,
                             DeusSourceAst *out, DeusDiagnostic *diagnostic) {
    Parser parser; size_t index, flow_line = SIZE_MAX, item_capacity = 0u;
    int limits_seen = 0; unsigned active_depth = 0u;
    if (!out || !diagnostic || (!source && length)) return 0;
    memset(out, 0, sizeof(*out)); memset(diagnostic, 0, sizeof(*diagnostic));
    parser.ast = out; parser.diagnostic = diagnostic; parser.capacity = 0u;
    if (length > DEUS_MAX_SECTION) return fail(&parser, 1u, 1u, "source is too large");
    out->source = (char *)malloc(length + 1u);
    if (!out->source) return fail(&parser, 1u, 1u, "out of memory");
    if (length) memcpy(out->source, source, length); out->source[length] = '\0'; out->source_length = length;
    if (!scan_lines(&parser)) goto failed;
    for (index = 0u; index < out->line_count; index++) {
        DeusSourceLogicalLine *line = &out->lines[index];
        size_t start, end, name_start, name_end;
        if (line->kind != DEUS_SOURCE_LINE_CONTENT) continue;
        flow_line = index; start = line->content_span.start.offset; end = line->content_span.end.offset; trim_span(out->source, &start, &end);
        if (line->depth || end - start < 6u || memcmp(out->source + start, "flow ", 5u) || out->source[end - 1u] != ':') {
            fail(&parser, line->span.start.line, line->content_span.start.column, "expected `flow <name>:` at top level"); goto failed;
        }
        name_start = start + 5u; name_end = end - 1u; trim_span(out->source, &name_start, &name_end);
        if (!identifier(out->source, name_start, name_end)) { fail(&parser, line->span.start.line, 6u, "invalid flow name"); goto failed; }
        out->flow.name = (char *)malloc(name_end - name_start + 1u);
        if (!out->flow.name) { fail(&parser, line->span.start.line, 1u, "out of memory"); goto failed; }
        memcpy(out->flow.name, out->source + name_start, name_end - name_start); out->flow.name[name_end - name_start] = '\0';
        out->flow.name_span = span_at(out->source, name_start, name_end); out->flow.span.start = line->span.start; break;
    }
    if (flow_line == SIZE_MAX) { fail(&parser, 1u, 1u, "expected `flow <name>:`"); goto failed; }
    for (index = flow_line + 1u; index < out->line_count;) {
        DeusSourceLogicalLine *line = &out->lines[index];
        if (line->kind != DEUS_SOURCE_LINE_CONTENT) {
            line->owner = active_depth == 2u ? DEUS_SOURCE_OWNER_LIMITS : DEUS_SOURCE_OWNER_FLOW; line->depth = active_depth ? active_depth : 1u; index++; continue;
        }
        if (line->depth == 0u) { fail(&parser, line->span.start.line, 1u, "unexpected top-level content after flow declaration"); goto failed; }
        if (line->depth > active_depth + 1u && active_depth) { fail(&parser, line->span.start.line, 1u, "indentation jumps more than one block level"); goto failed; }
        if (line->depth != 1u) { fail(&parser, line->span.start.line, 1u, "flow items must be indented by four spaces"); goto failed; }
        active_depth = 1u; line->owner = DEUS_SOURCE_OWNER_FLOW;
        {
            size_t start = line->content_span.start.offset, end = line->content_span.end.offset; DeusSourceFlowItem item;
            trim_span(out->source, &start, &end); memset(&item, 0, sizeof(item)); item.span = line->span;
            if (end - start == 7u && !memcmp(out->source + start, "limits:", 7u)) {
                size_t entry_capacity = 0u; unsigned seen = 0u; size_t entry_start;
                if (limits_seen) { fail(&parser, line->span.start.line, line->content_span.start.column, "multiple `limits` blocks are not allowed"); goto failed; }
                limits_seen = 1; item.kind = DEUS_SOURCE_FLOW_LIMITS; item.as.limits.span.start = line->span.start; entry_start = ++index; active_depth = 2u;
                while (index < out->line_count) {
                    DeusSourceLogicalLine *entry_line = &out->lines[index];
                    if (entry_line->kind != DEUS_SOURCE_LINE_CONTENT) { entry_line->owner = DEUS_SOURCE_OWNER_LIMITS; entry_line->depth = 2u; index++; continue; }
                    if (entry_line->depth <= 1u) break;
                    if (entry_line->depth != 2u) { free(item.as.limits.entries); fail(&parser, entry_line->span.start.line, 1u, "limits entries must be indented by eight spaces"); goto failed; }
                    entry_line->owner = DEUS_SOURCE_OWNER_LIMITS;
                    if (!parse_limit_entry(&parser, entry_line, &item.as.limits, &entry_capacity, &seen)) { free(item.as.limits.entries); goto failed; }
                    item.as.limits.span.end = entry_line->span.end; index++;
                }
                if (!item.as.limits.entry_count) { (void)entry_start; fail(&parser, line->span.start.line + 1u, 1u, "expected an indented limits entry"); goto failed; }
                item.span.end = item.as.limits.span.end;
                if (!append_item(&parser, item, &item_capacity)) { free(item.as.limits.entries); goto failed; } active_depth = 1u; continue;
            }
            item.kind = DEUS_SOURCE_FLOW_RAW; item.as.raw = line->span;
            if (!append_item(&parser, item, &item_capacity)) goto failed;
        }
        index++;
    }
    if (!out->flow.item_count) { fail(&parser, out->flow.span.start.line, 1u, "flow requires an indented body"); goto failed; }
    out->flow.span.end = out->flow.items[out->flow.item_count - 1u].span.end; return 1;
failed:
    deus_source_ast_free(out); return 0;
}
