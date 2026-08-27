#include "deus_json.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define JSON_MAX_DEPTH 64u
#define JSON_MAX_TOKENS 8192u

typedef enum { JT_OBJECT, JT_ARRAY, JT_STRING, JT_PRIMITIVE } JsonTokenKind;
typedef struct { JsonTokenKind kind; size_t start, end; int parent; } JsonToken;
typedef struct {
    const char *data; size_t length, position; unsigned depth;
    JsonToken tokens[JSON_MAX_TOKENS]; size_t count; char *error; size_t error_cap;
} JsonParser;

static int utf8_valid(const char *text, size_t length) {
    size_t at = 0u;
    while (at < length) {
        unsigned char first = (unsigned char)text[at++]; uint32_t codepoint; size_t remaining;
        if (first < 0x80u) continue;
        if ((first & 0xE0u) == 0xC0u) { codepoint = first & 0x1Fu; remaining = 1u; }
        else if ((first & 0xF0u) == 0xE0u) { codepoint = first & 0x0Fu; remaining = 2u; }
        else if ((first & 0xF8u) == 0xF0u) { codepoint = first & 0x07u; remaining = 3u; }
        else return 0;
        if (at + remaining > length) return 0;
        for (size_t part = 0u; part < remaining; part++) {
            unsigned char byte = (unsigned char)text[at++];
            if ((byte & 0xC0u) != 0x80u) return 0;
            codepoint = (codepoint << 6) | (byte & 0x3Fu);
        }
        if ((remaining == 1u && codepoint < 0x80u) || (remaining == 2u && codepoint < 0x800u) ||
            (remaining == 3u && codepoint < 0x10000u) || codepoint > 0x10FFFFu ||
            (codepoint >= 0xD800u && codepoint <= 0xDFFFu)) return 0;
    }
    return 1;
}

static int json_fail(JsonParser *parser, const char *message) {
    if (parser->error_cap) snprintf(parser->error, parser->error_cap, "%s at byte %zu", message, parser->position);
    return -1;
}

static void skip_space(JsonParser *parser) {
    while (parser->position < parser->length) {
        char c = parser->data[parser->position];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
        parser->position++;
    }
}

static int add_token(JsonParser *parser, JsonTokenKind kind, size_t start, int parent) {
    if (parser->count == JSON_MAX_TOKENS) return json_fail(parser, "JSON token limit exceeded");
    size_t index = parser->count++;
    parser->tokens[index] = (JsonToken){kind, start, start, parent}; return (int)index;
}

static int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int parse_string(JsonParser *parser, int parent) {
    size_t start; int token;
    if (parser->data[parser->position] != '"') return json_fail(parser, "expected JSON string");
    parser->position++; start = parser->position; token = add_token(parser, JT_STRING, start, parent);
    if (token < 0) return -1;
    while (parser->position < parser->length) {
        unsigned char c = (unsigned char)parser->data[parser->position++];
        if (c == '"') { parser->tokens[token].end = parser->position - 1u; return token; }
        if (c < 0x20u) return json_fail(parser, "control character in JSON string");
        if (c == '\\') {
            if (parser->position >= parser->length) return json_fail(parser, "unfinished JSON escape");
            char escape = parser->data[parser->position++];
            if (escape == 'u') {
                if (parser->length - parser->position < 4u) return json_fail(parser, "truncated Unicode escape");
                for (unsigned index = 0; index < 4u; index++)
                    if (hex_value(parser->data[parser->position + index]) < 0) return json_fail(parser, "invalid Unicode escape");
                parser->position += 4u;
            } else if (!strchr("\"\\/bfnrt", escape)) return json_fail(parser, "invalid JSON escape");
        }
    }
    return json_fail(parser, "unterminated JSON string");
}

static int valid_number(const char *value, size_t length) {
    size_t at = 0u;
    if (at < length && value[at] == '-') at++;
    if (at == length) return 0;
    if (value[at] == '0') at++;
    else {
        if (value[at] < '1' || value[at] > '9') return 0;
        while (at < length && value[at] >= '0' && value[at] <= '9') at++;
    }
    if (at < length && value[at] == '.') {
        at++; if (at == length || value[at] < '0' || value[at] > '9') return 0;
        while (at < length && value[at] >= '0' && value[at] <= '9') at++;
    }
    if (at < length && (value[at] == 'e' || value[at] == 'E')) {
        at++; if (at < length && (value[at] == '+' || value[at] == '-')) at++;
        if (at == length || value[at] < '0' || value[at] > '9') return 0;
        while (at < length && value[at] >= '0' && value[at] <= '9') at++;
    }
    return at == length;
}

static int parse_value(JsonParser *parser, int parent);

static int parse_container(JsonParser *parser, int parent, int object) {
    char close = object ? '}' : ']'; int token = add_token(parser, object ? JT_OBJECT : JT_ARRAY, parser->position, parent);
    if (token < 0) return -1;
    if (++parser->depth > JSON_MAX_DEPTH) return json_fail(parser, "JSON depth limit exceeded");
    parser->position++; skip_space(parser);
    if (parser->position < parser->length && parser->data[parser->position] == close) {
        parser->position++; parser->tokens[token].end = parser->position; parser->depth--; return token;
    }
    for (;;) {
        if (object) {
            if (parser->position >= parser->length || parser->data[parser->position] != '"' || parse_string(parser, token) < 0) return -1;
            skip_space(parser);
            if (parser->position >= parser->length || parser->data[parser->position++] != ':') return json_fail(parser, "expected ':' after JSON key");
            skip_space(parser);
        }
        if (parse_value(parser, token) < 0) return -1;
        skip_space(parser);
        if (parser->position >= parser->length) return json_fail(parser, "unterminated JSON container");
        char delimiter = parser->data[parser->position++];
        if (delimiter == close) { parser->tokens[token].end = parser->position; parser->depth--; return token; }
        if (delimiter != ',') return json_fail(parser, "expected ',' in JSON container");
        skip_space(parser);
    }
}

static int parse_value(JsonParser *parser, int parent) {
    skip_space(parser);
    if (parser->position >= parser->length) return json_fail(parser, "expected JSON value");
    char c = parser->data[parser->position];
    if (c == '{') return parse_container(parser, parent, 1);
    if (c == '[') return parse_container(parser, parent, 0);
    if (c == '"') return parse_string(parser, parent);
    size_t start = parser->position;
    while (parser->position < parser->length && !strchr(" \t\r\n,]}", parser->data[parser->position])) parser->position++;
    size_t length = parser->position - start;
    if (!length || (!valid_number(parser->data + start, length) &&
        !(length == 4u && (!memcmp(parser->data + start, "true", 4u) || !memcmp(parser->data + start, "null", 4u))) &&
        !(length == 5u && !memcmp(parser->data + start, "false", 5u)))) return json_fail(parser, "invalid JSON primitive");
    int token = add_token(parser, JT_PRIMITIVE, start, parent);
    if (token >= 0) parser->tokens[token].end = parser->position;
    return token;
}

static int decode_string(const char *source, size_t length, char **out, size_t *out_length) {
    char *decoded = (char *)malloc(length + 1u); size_t used = 0u;
    if (!decoded) return 0;
    for (size_t at = 0; at < length;) {
        unsigned char c = (unsigned char)source[at++];
        if (c != '\\') { decoded[used++] = (char)c; continue; }
        char escape = source[at++];
        if (escape == 'b') decoded[used++] = '\b'; else if (escape == 'f') decoded[used++] = '\f';
        else if (escape == 'n') decoded[used++] = '\n'; else if (escape == 'r') decoded[used++] = '\r';
        else if (escape == 't') decoded[used++] = '\t'; else if (escape != 'u') decoded[used++] = escape;
        else {
            uint32_t codepoint = 0u;
            for (unsigned part = 0; part < 4u; part++) codepoint = (codepoint << 4) | (uint32_t)hex_value(source[at++]);
            if (codepoint >= 0xD800u && codepoint <= 0xDBFFu) {
                if (at + 6u > length || source[at] != '\\' || source[at + 1u] != 'u') { free(decoded); return 0; }
                at += 2u; uint32_t low = 0u;
                for (unsigned part = 0; part < 4u; part++) low = (low << 4) | (uint32_t)hex_value(source[at++]);
                if (low < 0xDC00u || low > 0xDFFFu) { free(decoded); return 0; }
                codepoint = 0x10000u + ((codepoint - 0xD800u) << 10) + (low - 0xDC00u);
            } else if (codepoint >= 0xDC00u && codepoint <= 0xDFFFu) { free(decoded); return 0; }
            if (codepoint < 0x80u) decoded[used++] = (char)codepoint;
            else if (codepoint < 0x800u) { decoded[used++] = (char)(0xC0u | (codepoint >> 6)); decoded[used++] = (char)(0x80u | (codepoint & 0x3Fu)); }
            else if (codepoint < 0x10000u) { decoded[used++] = (char)(0xE0u | (codepoint >> 12)); decoded[used++] = (char)(0x80u | ((codepoint >> 6) & 0x3Fu)); decoded[used++] = (char)(0x80u | (codepoint & 0x3Fu)); }
            else { decoded[used++] = (char)(0xF0u | (codepoint >> 18)); decoded[used++] = (char)(0x80u | ((codepoint >> 12) & 0x3Fu)); decoded[used++] = (char)(0x80u | ((codepoint >> 6) & 0x3Fu)); decoded[used++] = (char)(0x80u | (codepoint & 0x3Fu)); }
        }
    }
    decoded[used] = '\0'; *out = decoded; *out_length = used; return 1;
}

static int key_matches(const JsonParser *parser, size_t token_index, const char *key, size_t key_length) {
    const JsonToken *token = &parser->tokens[token_index]; char *decoded; size_t decoded_length;
    if (!decode_string(parser->data + token->start, token->end - token->start, &decoded, &decoded_length)) return 0;
    int matches = decoded_length == key_length && !memcmp(decoded, key, key_length); free(decoded); return matches;
}

static int object_child(const JsonParser *parser, int object, const char *key, size_t key_length) {
    for (size_t index = (size_t)object + 1u; index < parser->count; index++) {
        if (parser->tokens[index].parent != object || parser->tokens[index].kind != JT_STRING) continue;
        size_t value = index + 1u;
        while (value < parser->count && parser->tokens[value].parent != object) value++;
        if (value < parser->count && key_matches(parser, index, key, key_length)) return (int)value;
        index = value;
    }
    return -1;
}

static int array_child(const JsonParser *parser, int array, size_t wanted) {
    size_t current = 0u;
    for (size_t index = (size_t)array + 1u; index < parser->count; index++)
        if (parser->tokens[index].parent == array && current++ == wanted) return (int)index;
    return -1;
}

static const char *scalar_kind_name(DeusJsonScalarKind kind) {
    switch (kind) {
        case DEUS_JSON_NULL: return "Null";
        case DEUS_JSON_BOOL: return "Bool";
        case DEUS_JSON_I64: return "I64";
        case DEUS_JSON_STRING: return "String";
        default: return "unknown";
    }
}
void deus_json_scalar_dispose(DeusJsonScalar *scalar) {
    if (!scalar) return;
    free(scalar->string);
    memset(scalar, 0, sizeof(*scalar));
}

int deus_json_extract_scalar(const char *json, size_t json_length,
                             const char *path, size_t path_length,
                             DeusJsonScalar *out, char *error, size_t error_cap) {
    JsonParser parser = {json, json_length, 0u, 0u, {{0}}, 0u, error, error_cap};
    memset(out, 0, sizeof(*out)); if (error_cap) error[0] = '\0';
    if (!utf8_valid(json, json_length)) { if (error_cap) snprintf(error, error_cap, "JSON input is not valid UTF-8"); return 0; }
    int current = parse_value(&parser, -1); skip_space(&parser);
    if (current < 0 || parser.position != parser.length) { if (current >= 0) json_fail(&parser, "trailing JSON content"); return 0; }
    if (!path_length || path[0] != '$') { if (error_cap) snprintf(error, error_cap, "JSON path must start with '$'"); return 0; }
    size_t at = 1u;
    while (at < path_length) {
        if (path[at] == '.') {
            size_t start = ++at;
            while (at < path_length && path[at] != '.' && path[at] != '[') at++;
            if (at == start || parser.tokens[current].kind != JT_OBJECT ||
                (current = object_child(&parser, current, path + start, at - start)) < 0) {
                if (error_cap) snprintf(error, error_cap, "JSON object path not found");
                return 0;
            }
        } else if (path[at] == '[') {
            size_t index = 0u; at++;
            if (at == path_length || path[at] < '0' || path[at] > '9') { if (error_cap) snprintf(error, error_cap, "invalid JSON array index"); return 0; }
            while (at < path_length && path[at] >= '0' && path[at] <= '9') {
                if (index > (SIZE_MAX - 9u) / 10u) { if (error_cap) snprintf(error, error_cap, "JSON array index overflow"); return 0; }
                index = index * 10u + (size_t)(path[at++] - '0');
            }
            if (at >= path_length || path[at++] != ']' || parser.tokens[current].kind != JT_ARRAY ||
                (current = array_child(&parser, current, index)) < 0) { if (error_cap) snprintf(error, error_cap, "JSON array path not found"); return 0; }
        } else { if (error_cap) snprintf(error, error_cap, "invalid JSON path syntax"); return 0; }
    }
    const JsonToken *token = &parser.tokens[current];
    if (token->kind == JT_STRING) {
        out->kind = DEUS_JSON_STRING;
        if (!decode_string(json + token->start, token->end - token->start, &out->string, &out->string_length)) { if (error_cap) snprintf(error, error_cap, "invalid JSON Unicode scalar"); return 0; }
        return 1;
    }
    if (token->kind != JT_PRIMITIVE) { if (error_cap) snprintf(error, error_cap, "JSON path resolved to a compound value"); return 0; }
    size_t length = token->end - token->start; const char *value = json + token->start;
    if (length == 4u && !memcmp(value, "null", 4u)) { out->kind = DEUS_JSON_NULL; return 1; }
    if (length == 4u && !memcmp(value, "true", 4u)) { out->kind = DEUS_JSON_BOOL; out->boolean = 1; return 1; }
    if (length == 5u && !memcmp(value, "false", 5u)) { out->kind = DEUS_JSON_BOOL; return 1; }
    if (memchr(value, '.', length) || memchr(value, 'e', length) || memchr(value, 'E', length) || length >= 64u) {
        if (error_cap) snprintf(error, error_cap, "JSON number is not an i64");
        return 0;
    }
    char number[64]; memcpy(number, value, length); number[length] = '\0'; errno = 0; char *end = NULL;
    long long integer = strtoll(number, &end, 10);
    if (errno == ERANGE || !end || *end) { if (error_cap) snprintf(error, error_cap, "JSON integer exceeds i64"); return 0; }
    out->kind = DEUS_JSON_I64; out->integer = (int64_t)integer; return 1;
}
int deus_json_validate_scalar_contract(const char *json, size_t json_length,
                                       const DeusJsonScalarContract *fields,
                                       size_t field_count, char *error, size_t error_cap) {
    size_t index;
    if (error_cap) error[0] = '\0';
    if (!json || !fields || !field_count) {
        if (error_cap) snprintf(error, error_cap, "JSON contract requires at least one field");
        return 0;
    }
    for (index = 0u; index < field_count; index++) {
        DeusJsonScalar value;
        const DeusJsonScalarContract *field = &fields[index];
        if (!field->path || !field->path_length || field->kind > DEUS_JSON_STRING) {
            if (error_cap) snprintf(error, error_cap, "invalid JSON contract field %zu", index + 1u);
            return 0;
        }
        if (!deus_json_extract_scalar(json, json_length, field->path, field->path_length,
                                      &value, error, error_cap)) {
            if (error_cap && error[0]) {
                char detail[192];
                snprintf(detail, sizeof(detail), "%s", error);
                snprintf(error, error_cap, "JSON contract field %zu failed: %s", index + 1u, detail);
            }
            return 0;
        }
        if (value.kind != field->kind && !(field->nullable && value.kind == DEUS_JSON_NULL)) {
            if (error_cap) snprintf(error, error_cap,
                                    "JSON contract field %zu expected %s, got %s",
                                    index + 1u, scalar_kind_name(field->kind),
                                    scalar_kind_name(value.kind));
            deus_json_scalar_dispose(&value);
            return 0;
        }
        deus_json_scalar_dispose(&value);
    }
    return 1;
}