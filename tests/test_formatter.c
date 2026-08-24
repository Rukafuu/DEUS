#include "deus.h"
#include "deus_formatter.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int require(int condition, const char *message) {
    if (!condition) fprintf(stderr, "formatter test: %s\n", message);
    return condition;
}

static int has_string_operand(uint8_t opcode) {
    return opcode == DEUS_OMNI || opcode == DEUS_HUNT || opcode == DEUS_FORK ||
           opcode == DEUS_REAP || opcode == DEUS_CONST ||
           opcode == DEUS_JSON_PATH || opcode == DEUS_RECORD_SET ||
           opcode == DEUS_RECORD_GET || opcode == DEUS_RECORD_GET_OPTIONAL;
}

static int same_string(const DeusProgram *left, uint32_t left_index,
                       const DeusProgram *right, uint32_t right_index) {
    const DeusString *a, *b;
    if (left_index >= left->string_count || right_index >= right->string_count)
        return 0;
    a = &left->strings[left_index]; b = &right->strings[right_index];
    return a->len == b->len && (!a->len || !memcmp(a->data, b->data, a->len));
}

static int same_program(const DeusProgram *left, const DeusProgram *right) {
    uint32_t index;
    if (left->code_count != right->code_count) return 0;
    for (index = 0u; index < left->code_count; index++) {
        const DeusInstruction *a = &left->code[index];
        const DeusInstruction *b = &right->code[index];
        if (a->opcode != b->opcode || a->immediate != b->immediate) return 0;
        if (has_string_operand(a->opcode)) {
            if (!same_string(left, a->operand, right, b->operand)) return 0;
        } else if (a->operand != b->operand) return 0;
    }
    return 1;
}

static int canonical_text(const char *text, size_t length) {
    size_t index, line_start = 0u;
    if (!length || text[length - 1u] != '\n') return 0;
    if (length > 1u && text[length - 2u] == '\n') return 0;
    for (index = 0u; index < length; index++) {
        if (text[index] == '\r' || text[index] == '\t') return 0;
        if (text[index] == '\n') {
            if (index > line_start && text[index - 1u] == ' ') return 0;
            line_start = index + 1u;
        }
    }
    return 1;
}

static int round_trip(const char *name, const char *source) {
    DeusDiagnostic diagnostic = {0}; DeusProgram before = {0}, after = {0};
    char *formatted = NULL, *second = NULL; size_t length = 0u, second_length = 0u;
    int ok = 0;
    if (!deus_parse_source(source, strlen(source), &before, &diagnostic)) {
        fprintf(stderr, "formatter test: %s input did not parse at %u:%u: %s\n",
                name, diagnostic.line, diagnostic.column, diagnostic.message); return 0;
    }
    if (!deus_format_source(source, strlen(source), &formatted, &length, &diagnostic)) {
        fprintf(stderr, "formatter test: %s did not format at %u:%u: %s\n",
                name, diagnostic.line, diagnostic.column, diagnostic.message); goto done;
    }
    if (!require(canonical_text(formatted, length), "formatter output is not canonical")) goto done;
    if ((strstr(source, "# owner comment") && !strstr(formatted, "# owner comment")) ||
        (strstr(source, "# body comment") && !strstr(formatted, "# body comment")) ||
        (strstr(source, "# legacy comment") && !strstr(formatted, "# legacy comment"))) {
        require(0, "formatter discarded a source comment"); goto done;
    }
    if (strstr(source, "# body comment") && !strstr(formatted, "\n\n    # body comment\n")) {
        require(0, "formatter changed blank-line or comment ownership"); goto done;
    }
    if (!deus_format_source(formatted, length, &second, &second_length, &diagnostic)) {
        fprintf(stderr, "formatter test: %s canonical output did not format: %s\n",
                name, diagnostic.message); goto done;
    }
    if (!require(length == second_length && !memcmp(formatted, second, length),
                 "formatting is not byte-idempotent")) goto done;
    if (!deus_parse_source(formatted, length, &after, &diagnostic)) {
        fprintf(stderr, "formatter test: %s formatted output did not parse at %u:%u: %s\n",
                name, diagnostic.line, diagnostic.column, diagnostic.message); goto done;
    }
    if (!require(same_program(&before, &after),
                 "parse-format-parse changed program semantics")) goto done;
    ok = 1;
done:
    free(formatted); free(second); deus_program_free(&before); deus_program_free(&after);
    return ok;
}

static int test_modern(void) {
    const char *source =
        "# owner comment\r\n"
        "flow search:\r\n"
        "    limits:   \r\n"
        "        workers 8\r\n"
        "        retry 3\r\n"
        "        backoff 100\r\n"
        "        rate 20\r\n"
        "\r\n"
        "    # body comment\r\n"
        "    bind title = \"# not a comment // nor this: [] {}\"\r\n"
        "    bind item = {\r\n"
        "        \"title\": title,\r\n"
        "        \"meta\": {\r\n"
        "            \"verified\": true,\r\n"
        "            \"tags\": [\r\n"
        "                \"elf\",\r\n"
        "                \"mage\"\r\n"
        "            ]\r\n"
        "        }\r\n"
        "    }\r\n"
        "    load item\r\n"
        "    emit   ";
    return round_trip("modern CRLF, limits, comments, and literals", source);
}

static int test_legacy(void) {
    const char *source =
        "  # legacy comment\r"
        " omni \"net.http2\"   \r"
        " genesis\r"
        " bind marker = \"text: # // [still string]\"\r"
        " bind nested = {\r"
        "      \"items\": [\r"
        "           marker,\r"
        "       {\"ok\": true}\r"
        "     ]\r"
        " }\r"
        " load nested\r"
        " emit\r"
        " halt       ";
    return round_trip("legacy CR, comments, and literals", source);
}

static int test_canonical_indentation(void) {
    const char *source =
        "flow main:\n"
        "    limits:   \n"
        "        workers 2  \n"
        "        retry 0\n"
        "    bind value = [\n"
        "        {\"ok\": true}   \n"
        "    ]\n"
        "    load value\n"
        "    emit   \n";
    const char *expected =
        "flow main:\n"
        "    limits:\n"
        "        workers 2\n"
        "        retry 0\n"
        "    bind value = [\n"
        "        {\"ok\": true}\n"
        "    ]\n"
        "    load value\n"
        "    emit\n";
    DeusDiagnostic diagnostic = {0}; char *formatted = NULL; size_t length = 0u;
    int ok = deus_format_source(source, strlen(source), &formatted, &length, &diagnostic) &&
             length == strlen(expected) && !memcmp(formatted, expected, length);
    free(formatted); return require(ok, "modern output is not canonical four-space indentation");
}

static int test_tabs_rejected(void) {
    const char *source = "flow main:\n\tlimits:\n        workers 2\n";
    DeusDiagnostic diagnostic = {0}; char *formatted = NULL; size_t length = 0u;
    int accepted = deus_format_source(source, strlen(source), &formatted, &length, &diagnostic);
    free(formatted);
    return require(!accepted && diagnostic.line == 2u && strstr(diagnostic.message, "tab") != NULL,
                   "leading tab must be rejected with a targeted diagnostic");
}

int main(void) {
    return test_modern() && test_canonical_indentation() && test_tabs_rejected() &&
           test_legacy() ? 0 : 1;
}
