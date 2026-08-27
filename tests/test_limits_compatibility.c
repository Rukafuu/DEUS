#include "deus.h"

#include <stdio.h>
#include <string.h>

static int failures;

static void fail(const char *name, const char *detail) {
    fprintf(stderr, "limits compatibility test [%s]: %s\n", name, detail);
    failures++;
}

static int compile_source(const char *name, const char *source,
                          DeusProgram *program) {
    DeusDiagnostic diagnostic = {0};
    memset(program, 0, sizeof(*program));
    if (!deus_parse_source(source, strlen(source), program, &diagnostic)) {
        fprintf(stderr,
                "limits compatibility test [%s]: unexpected %u:%u: %s\n",
                name, diagnostic.line, diagnostic.column, diagnostic.message);
        failures++;
        return 0;
    }
    return 1;
}

static void expect_rejection(const char *name, const char *source,
                             const char *message) {
    DeusProgram program = {0};
    DeusDiagnostic diagnostic = {0};
    if (deus_parse_source(source, strlen(source), &program, &diagnostic)) {
        deus_program_free(&program);
        fail(name, "source was unexpectedly accepted");
    } else if (!strstr(diagnostic.message, message)) {
        fprintf(stderr,
                "limits compatibility test [%s]: expected diagnostic "
                "containing '%s', got '%s'\n",
                name, message, diagnostic.message);
        failures++;
    }
}

static int has_string_operand(uint8_t opcode) {
    return opcode == DEUS_OMNI || opcode == DEUS_HUNT ||
           opcode == DEUS_REAP || opcode == DEUS_FORK ||
           opcode == DEUS_CONST || opcode == DEUS_JSON_PATH ||
           opcode == DEUS_RECORD_SET || opcode == DEUS_RECORD_GET ||
           opcode == DEUS_RECORD_GET_OPTIONAL;
}

static int strings_equal(const DeusString *left, const DeusString *right) {
    return left->len == right->len &&
           (left->len == 0u ||
            memcmp(left->data, right->data, left->len) == 0);
}

/* Compare observable instructions, resolving string-pool operands by value.
 * Pool indices and struct padding are intentionally not part of equivalence. */
static int programs_semantically_equal(const DeusProgram *left,
                                       const DeusProgram *right) {
    uint32_t index;
    if (left->code_count != right->code_count) return 0;
    for (index = 0u; index < left->code_count; index++) {
        const DeusInstruction *a = &left->code[index];
        const DeusInstruction *b = &right->code[index];
        if (a->opcode != b->opcode || a->immediate != b->immediate) return 0;
        if (has_string_operand(a->opcode)) {
            if (a->operand >= left->string_count ||
                b->operand >= right->string_count ||
                !strings_equal(&left->strings[a->operand],
                               &right->strings[b->operand])) return 0;
        } else if (a->operand != b->operand) {
            return 0;
        }
    }
    return 1;
}

static void expect_equivalent(const char *name, const char *left_source,
                              const char *right_source) {
    DeusProgram left;
    DeusProgram right;
    int left_ok = compile_source(name, left_source, &left);
    int right_ok = compile_source(name, right_source, &right);
    if (left_ok && right_ok && !programs_semantically_equal(&left, &right))
        fail(name, "compiled programs are not semantically equivalent");
    if (left_ok) deus_program_free(&left);
    if (right_ok) deus_program_free(&right);
}

static void test_frozen_public_contract(void) {
    static const struct {
        uint8_t actual;
        uint8_t expected;
    } opcodes[] = {
        {DEUS_OMNI, 0x01}, {DEUS_GENESIS, 0x02}, {DEUS_HUNT, 0x03},
        {DEUS_REAP, 0x04}, {DEUS_HALT, 0x05}, {DEUS_EMIT, 0x06},
        {DEUS_FORK, 0x07}, {DEUS_AWAIT, 0x08}, {DEUS_JOIN, 0x09},
        {DEUS_LIMIT, 0x0A}, {DEUS_RETRY, 0x0B}, {DEUS_BACKOFF, 0x0C},
        {DEUS_RATE, 0x0D}, {DEUS_CONST, 0x0E}, {DEUS_BIND, 0x0F},
        {DEUS_LOAD, 0x10}, {DEUS_CONST_I64, 0x11},
        {DEUS_CONST_BOOL, 0x12}, {DEUS_CONST_NULL, 0x13},
        {DEUS_CONST_RECORD, 0x14}, {DEUS_CONST_LIST, 0x15},
        {DEUS_LIST_PUSH, 0x16}, {DEUS_RECORD_SET, 0x17},
        {DEUS_RECORD_GET, 0x18}, {DEUS_LIST_AT, 0x19},
        {DEUS_JSON_PATH, 0x1A}, {DEUS_RECORD_GET_OPTIONAL, 0x1B},
        {DEUS_LIST_AT_OPTIONAL, 0x1C}, {DEUS_URL_ENCODE, 0x1D},
        {DEUS_URL_JOIN, 0x1E}, {DEUS_HUNT_VALUE, 0x1F},
        {DEUS_BOOL_NOT, 0x20}, {DEUS_BOOL_AND, 0x21},
        {DEUS_BOOL_OR, 0x22}, {DEUS_EQUAL, 0x23},
        {DEUS_NOT_EQUAL, 0x24}, {DEUS_LESS, 0x25},
        {DEUS_LESS_EQUAL, 0x26}, {DEUS_GREATER, 0x27},
        {DEUS_GREATER_EQUAL, 0x28}, {DEUS_COALESCE, 0x29},
        {DEUS_TO_TEXT, 0x2A}, {DEUS_TO_I64, 0x2B},
        {DEUS_TO_BOOL, 0x2C}, {DEUS_HOST_CALL, 0x2D}, {DEUS_DEBUG, 0x2E}    };
    size_t index;
    _Static_assert(DEUS_ABI_VERSION == 3u, "bytecode ABI changed");
    _Static_assert(DEUS_HOST_ABI_VERSION == 2u, "host ABI changed");
    _Static_assert(DEUS_HEADER_SIZE == 40u, "header size changed");
    for (index = 0u; index < sizeof(opcodes) / sizeof(opcodes[0]); index++) {
        if (opcodes[index].actual != opcodes[index].expected) {
            fail("opcodes", "a public opcode numeric value changed");
            break;
        }
    }
}

static void test_source_compatibility(void) {
    static const char legacy_flat[] =
        "genesis\nlimit 8\nretry 3\nbackoff 100\nrate 20\n"
        "bind query = \"frieren\"\nload query\nemit\nhalt\n";
    static const char modern[] =
        "flow main:\n"
        "    limits:\n"
        "        workers 8\n"
        "        retry 3\n"
        "        backoff 100\n"
        "        rate 20\n"
        "    bind query = \"frieren\"\n"
        "    load query\n"
        "    emit\n";
    static const char varied[] =
        "flow main:\n"
        "    limits:\n"
        "        rate 20\n"
        "        workers 8\n"
        "        backoff 100\n"
        "        retry 3\n"
        "    bind query = \"frieren\"\n"
        "    load query\n"
        "    emit\n";
    static const char legacy_inside_flow[] =
        "flow main:\n"
        "    limit 8\n"
        "    retry 3\n"
        "    backoff 100\n"
        "    rate 20\n"
        "    bind query = \"frieren\"\n"
        "    load query\n"
        "    emit\n";
    DeusProgram program;
    if (compile_source("legacy flat", legacy_flat, &program))
        deus_program_free(&program);
    expect_equivalent("modern and legacy flat", modern, legacy_flat);
    expect_equivalent("varied modern order is canonical", varied, modern);
    expect_equivalent("legacy settings inside flow", legacy_inside_flow,
                      legacy_flat);
    expect_rejection("mixed modern and legacy workers",
                     "flow main:\n    limit 8\n    limits:\n"
                     "        workers 8\n",
                     "cannot appear both");
    expect_rejection("mixed modern and legacy retry",
                     "flow main:\n    limits:\n        retry 3\n"
                     "    retry 3\n",
                     "cannot appear both");
}

static void test_binary_round_trip(void) {
    static const char source[] =
        "flow main:\n    limits:\n        rate 20\n"
        "        workers 8\n        retry 3\n        backoff 100\n"
        "    bind query = \"frieren\"\n    load query\n    emit\n";
    DeusProgram original;
    DeusProgram decoded = {0};
    const char *temp_path = "deus_limits_compatibility_test.deusb";
    char error[192] = {0};
    int original_ok = compile_source("binary round trip", source, &original);
    if (!original_ok) return;
    (void)remove(temp_path);

    if (!deus_write_binary(&original, temp_path, error, sizeof(error))) {
        fail("binary round trip", error);
    } else if (!deus_read_binary(temp_path, &decoded, error, sizeof(error))) {
        fail("binary round trip", error);
    } else if (!programs_semantically_equal(&original, &decoded)) {
        fail("binary round trip", "decoded program changed semantics");
    }

    deus_program_free(&decoded);
    deus_program_free(&original);
    if (remove(temp_path))
        fail("binary round trip", "temporary bytecode could not be removed");
}

int main(void) {
    test_frozen_public_contract();
    test_source_compatibility();
    test_binary_round_trip();
    return failures ? 1 : 0;
}
