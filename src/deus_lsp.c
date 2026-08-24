#include "deus.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

typedef struct {
    char *uri;
    char *text;
} Document;

static void json_string(FILE *out, const char *text) {
    fputc('"', out);
    for (; text && *text; text++) {
        unsigned char c = (unsigned char)*text;
        if (c == '"' || c == '\\') { fputc('\\', out); fputc(c, out); }
        else if (c == '\n') fputs("\\n", out);
        else if (c == '\r') fputs("\\r", out);
        else if (c == '\t') fputs("\\t", out);
        else if (c < 0x20u) fprintf(out, "\\u%04x", c);
        else fputc(c, out);
    }
    fputc('"', out);
}

static void send_body(const char *body) {
    printf("Content-Length: %zu\r\n\r\n%s", strlen(body), body);
    fflush(stdout);
}

static void send_result(const char *id, const char *result) {
    size_t size = strlen(id) + strlen(result) + 48u;
    char *body = (char *)malloc(size);
    if (!body) return;
    (void)snprintf(body, size, "{\"jsonrpc\":\"2.0\",\"id\":%s,\"result\":%s}", id, result);
    send_body(body); free(body);
}

static char *read_message(void) {
    char header[512]; size_t length = 0u; char *body;
    while (fgets(header, sizeof(header), stdin)) {
        if (!strcmp(header, "\n") || !strcmp(header, "\r\n")) break;
        if (!strncmp(header, "Content-Length:", 15u)) length = (size_t)strtoull(header + 15u, NULL, 10);
    }
    if (!length || length > 16u * 1024u * 1024u) return NULL;
    body = (char *)malloc(length + 1u); if (!body) return NULL;
    if (fread(body, 1u, length, stdin) != length) { free(body); return NULL; }
    body[length] = '\0'; return body;
}

static const char *field(const char *json, const char *name) {
    char key[96]; int written = snprintf(key, sizeof(key), "\"%s\"", name);
    const char *at = written > 0 && (size_t)written < sizeof(key) ? strstr(json, key) : NULL;
    if (!at) return NULL;
    at += strlen(key);
    while (isspace((unsigned char)*at)) at++;
    if (*at++ != ':') return NULL;
    while (isspace((unsigned char)*at)) at++;
    return at;
}

static char *decode_string(const char *value) {
    size_t capacity, used = 0u; char *out;
    if (!value || *value != '"') return NULL;
    capacity = strlen(value) + 1u; out = (char *)malloc(capacity); if (!out) return NULL;
    for (value++; *value && *value != '"'; value++) {
        if (*value == '\\') {
            value++; if (!*value) break;
            if (*value == 'n') out[used++] = '\n';
            else if (*value == 'r') out[used++] = '\r';
            else if (*value == 't') out[used++] = '\t';
            else if (*value == 'u') {
                unsigned code = 0u; int ok = 1;
                for (int i = 0; i < 4; i++) { char c = value[i + 1];
                    if (c >= '0' && c <= '9') code = code * 16u + (unsigned)(c - '0');
                    else if (c >= 'a' && c <= 'f') code = code * 16u + (unsigned)(c - 'a' + 10);
                    else if (c >= 'A' && c <= 'F') code = code * 16u + (unsigned)(c - 'A' + 10); else ok = 0;
                }
                if (ok && code < 0x80u) out[used++] = (char)code; else out[used++] = '?'; value += 4;
            } else out[used++] = *value;
        } else out[used++] = *value;
    }
    out[used] = '\0'; return out;
}

static char *request_id(const char *json) {
    const char *at = field(json, "id"), *end; char *id; size_t length;
    if (!at) return NULL;
    if (*at == '"') { end = at + 1; while (*end && (*end != '"' || end[-1] == '\\')) end++; if (*end) end++; }
    else { end = at; while (*end && (isdigit((unsigned char)*end) || *end == '-')) end++; }
    length = (size_t)(end - at); if (!length) return NULL;
    id = (char *)malloc(length + 1u); if (!id) return NULL;
    memcpy(id, at, length); id[length] = '\0'; return id;
}

static void replace_document(Document *document, const char *json) {
    char *uri = decode_string(field(json, "uri"));
    char *text = decode_string(field(json, "text"));
    if (uri) { free(document->uri); document->uri = uri; }
    if (text) { free(document->text); document->text = text; }
}

static void publish_diagnostics(const Document *document) {
    DeusProgram program; DeusDiagnostic diagnostic = {0}; int valid;
    if (!document->uri || !document->text) return;
    valid = deus_parse_source(document->text, strlen(document->text), &program, &diagnostic);
    fputs("Content-Length: ", stdout);
    {
        FILE *tmp = tmpfile(); long length; char *body;
        if (!tmp) return;
        fputs("{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/publishDiagnostics\",\"params\":{\"uri\":", tmp);
        json_string(tmp, document->uri); fputs(",\"diagnostics\":[", tmp);
        if (!valid) {
            unsigned line = diagnostic.line ? diagnostic.line - 1u : 0u;
            unsigned column = diagnostic.column ? diagnostic.column - 1u : 0u;
            fprintf(tmp, "{\"range\":{\"start\":{\"line\":%u,\"character\":%u},\"end\":{\"line\":%u,\"character\":%u}},\"severity\":1,\"source\":\"deus\",\"message\":",
                    line, column, line, column + 1u); json_string(tmp, diagnostic.message); fputc('}', tmp);
        }
        fputs("]}}", tmp); fflush(tmp); length = ftell(tmp); rewind(tmp);
        body = (char *)malloc((size_t)length + 1u); if (!body) { fclose(tmp); return; }
        if (fread(body, 1u, (size_t)length, tmp) != (size_t)length) { free(body); fclose(tmp); return; }
        body[length] = '\0'; fclose(tmp); fprintf(stdout, "%ld\r\n\r\n%s", length, body); fflush(stdout); free(body);
    }
    if (valid) deus_program_free(&program);
}

static int position(const char *json, unsigned *line, unsigned *character) {
    const char *at = field(json, "position"), *l, *c;
    if (!at) return 0;
    l = field(at, "line");
    c = field(at, "character");
    if (!l || !c) return 0;
    *line = (unsigned)strtoul(l, NULL, 10);
    *character = (unsigned)strtoul(c, NULL, 10);
    return 1;
}

static int word_at(const char *text, unsigned wanted_line, unsigned character, char *word, size_t capacity) {
    const char *line = text, *end, *cursor, *start; unsigned current = 0u; size_t length;
    while (current < wanted_line && *line) { if (*line++ == '\n') current++; }
    if (current != wanted_line) return 0;
    end = strchr(line, '\n');
    if (!end) end = line + strlen(line);
    cursor = line + character; if (cursor > end) cursor = end;
    while (cursor > line && !isalnum((unsigned char)*cursor) && *cursor != '_') cursor--;
    start = cursor; while (start > line && (isalnum((unsigned char)start[-1]) || start[-1] == '_')) start--;
    while (cursor < end && (isalnum((unsigned char)*cursor) || *cursor == '_')) cursor++;
    length = (size_t)(cursor - start); if (!length || length >= capacity) return 0;
    memcpy(word, start, length); word[length] = '\0'; return 1;
}

static const char *hover_text(const char *word) {
    static const struct { const char *word, *text; } entries[] = {
        {"flow", "Declares a named DEUS retrieval flow."}, {"bind", "Binds a typed value to a local name."},
        {"hunt", "Retrieves a document through an authorized host capability."}, {"reap", "Extracts data from a resolved document."},
        {"parallel", "Runs bounded retrieval operations concurrently."}, {"emit", "Emits the value currently on the stack."},
        {"omni", "Declares a host module requirement; it does not grant authority."}, {"limit", "Sets the maximum concurrent retrieval count."}
    };
    for (size_t i = 0u; i < sizeof(entries) / sizeof(entries[0]); i++) if (!strcmp(word, entries[i].word)) return entries[i].text;
    return NULL;
}

static int declaration_name(const char *line, const char *keyword, char *name, size_t capacity) {
    size_t keyword_length = strlen(keyword), length = 0u;
    if (strncmp(line, keyword, keyword_length) || !isspace((unsigned char)line[keyword_length])) return 0;
    line += keyword_length; while (isspace((unsigned char)*line)) line++;
    while ((isalnum((unsigned char)line[length]) || line[length] == '_') && length + 1u < capacity) length++;
    if (!length || isalnum((unsigned char)line[length]) || line[length] == '_') return 0;
    memcpy(name, line, length); name[length] = '\0'; return 1;
}

static void handle_hover(const char *id, const char *json, const Document *document) {
    unsigned line, character; char word[128], result[512]; const char *description;
    if (!document->text || !position(json, &line, &character) || !word_at(document->text, line, character, word, sizeof(word)) || !(description = hover_text(word))) {
        send_result(id, "null"); return;
    }
    (void)snprintf(result, sizeof(result), "{\"contents\":{\"kind\":\"markdown\",\"value\":\"`%s` — %s\"}}", word, description);
    send_result(id, result);
}

static void handle_symbols(const char *id, const Document *document) {
    FILE *tmp = tmpfile(); char *copy, *line, *next; unsigned row = 0u; int first = 1; long size; char *result;
    if (!tmp || !document->text) { if (tmp) fclose(tmp); send_result(id, "[]"); return; }
    copy = (char *)malloc(strlen(document->text) + 1u); if (!copy) { fclose(tmp); send_result(id, "[]"); return; }
    memcpy(copy, document->text, strlen(document->text) + 1u);
    fputc('[', tmp); line = copy;
    while (line) {
        char *trim = line, name[128]; int kind = 0; unsigned column, name_column;
        next = strchr(line, '\n'); if (next) *next++ = '\0'; while (*trim == ' ') trim++; column = (unsigned)(trim - line);
        if (declaration_name(trim, "flow", name, sizeof(name))) kind = 12;
        else if (declaration_name(trim, "bind", name, sizeof(name))) kind = 13;
        name_column = kind ? (unsigned)(strstr(trim, name) - line) : column;
        if (kind) { if (!first) fputc(',', tmp); first = 0; fputs("{\"name\":", tmp); json_string(tmp, name);
            fprintf(tmp, ",\"kind\":%d,\"range\":{\"start\":{\"line\":%u,\"character\":%u},\"end\":{\"line\":%u,\"character\":%zu}},\"selectionRange\":{\"start\":{\"line\":%u,\"character\":%u},\"end\":{\"line\":%u,\"character\":%u}}}",
                    kind, row, column, row, strlen(line), row, name_column, row, name_column + (unsigned)strlen(name)); }
        line = next; row++;
    }
    fputc(']', tmp); free(copy); fflush(tmp); size = ftell(tmp); rewind(tmp); result = (char *)malloc((size_t)size + 1u);
    if (!result || fread(result, 1u, (size_t)size, tmp) != (size_t)size) { free(result); fclose(tmp); send_result(id, "[]"); return; }
    result[size] = '\0'; fclose(tmp); send_result(id, result); free(result);
}

static void handle_definition(const char *id, const char *json, const Document *document) {
    unsigned wanted_line, character, row = 0u; char word[128]; const char *line;
    if (!document->text || !document->uri || !position(json, &wanted_line, &character) || !word_at(document->text, wanted_line, character, word, sizeof(word))) { send_result(id, "null"); return; }
    line = document->text;
    while (*line) {
        const char *end = strchr(line, '\n'); const char *trim = line; char name[128];
        if (!end) end = line + strlen(line);
        while (trim < end && *trim == ' ') trim++;
        if ((declaration_name(trim, "bind", name, sizeof(name)) || declaration_name(trim, "flow", name, sizeof(name))) && !strcmp(name, word)) {
            FILE *tmp = tmpfile(); long size; char *result; if (!tmp) break;
            fputs("{\"uri\":", tmp); json_string(tmp, document->uri);
            unsigned name_column = (unsigned)(strstr(trim, name) - line);
            fprintf(tmp, ",\"range\":{\"start\":{\"line\":%u,\"character\":%u},\"end\":{\"line\":%u,\"character\":%u}}}", row, name_column, row, name_column + (unsigned)strlen(name));
            fflush(tmp); size = ftell(tmp); rewind(tmp); result = (char *)malloc((size_t)size + 1u);
            if (result && fread(result, 1u, (size_t)size, tmp) == (size_t)size) { result[size] = '\0'; send_result(id, result); free(result); fclose(tmp); return; }
            free(result); fclose(tmp); break;
        }
        line = *end ? end + 1 : end; row++;
    }
    send_result(id, "null");
}

int main(void) {
    Document document = {0}; int shutdown = 0;
#ifdef _WIN32
    (void)_setmode(_fileno(stdin), _O_BINARY);
    (void)_setmode(_fileno(stdout), _O_BINARY);
#endif
    for (;;) {
        char *json = read_message(), *method, *id;
        if (!json) break;
        method = decode_string(field(json, "method"));
        id = request_id(json);
        if (method && !strcmp(method, "initialize") && id) send_result(id, "{\"capabilities\":{\"textDocumentSync\":1,\"hoverProvider\":true,\"definitionProvider\":true,\"documentSymbolProvider\":true},\"serverInfo\":{\"name\":\"deus-language-server\",\"version\":\"0.1.0\"}}");
        else if (method && (!strcmp(method, "textDocument/didOpen") || !strcmp(method, "textDocument/didChange"))) { replace_document(&document, json); publish_diagnostics(&document); }
        else if (method && !strcmp(method, "textDocument/didClose")) { free(document.uri); free(document.text); document.uri = document.text = NULL; }
        else if (method && !strcmp(method, "textDocument/hover") && id) handle_hover(id, json, &document);
        else if (method && !strcmp(method, "textDocument/documentSymbol") && id) handle_symbols(id, &document);
        else if (method && !strcmp(method, "textDocument/definition") && id) handle_definition(id, json, &document);
        else if (method && !strcmp(method, "shutdown") && id) { shutdown = 1; send_result(id, "null"); }
        else if (method && !strcmp(method, "exit")) { free(method); free(id); free(json); break; }
        else if (id) send_result(id, "null");
        free(method); free(id); free(json);
    }
    free(document.uri); free(document.text); return shutdown ? 0 : 1;
}
