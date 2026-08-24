#include "deus.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    const char *name;
    const char *source;
    const char *message;
    unsigned line;
    unsigned column;
} Rejection;

static int failures;

static void fail(const char *name, const char *detail) {
    fprintf(stderr, "limits adversarial test [%s]: %s\n", name, detail);
    failures++;
}

static int compile_source(const char *source, DeusProgram *program,
                          DeusDiagnostic *diagnostic) {
    memset(program, 0, sizeof(*program));
    memset(diagnostic, 0, sizeof(*diagnostic));
    return deus_parse_source(source, strlen(source), program, diagnostic);
}

static void expect_rejection(const Rejection *test) {
    DeusProgram program;
    DeusDiagnostic diagnostic;
    int accepted = compile_source(test->source, &program, &diagnostic);
    if (accepted) {
        deus_program_free(&program);
        fail(test->name, "source was unexpectedly accepted");
        return;
    }
    if (!strstr(diagnostic.message, test->message)) {
        fprintf(stderr, "limits adversarial test [%s]: expected diagnostic containing '%s', got '%s'\n",
                test->name, test->message, diagnostic.message);
        failures++;
    }
    if (test->line && diagnostic.line != test->line) {
        fprintf(stderr, "limits adversarial test [%s]: expected line %u, got %u\n",
                test->name, test->line, diagnostic.line);
        failures++;
    }
    if (test->column && diagnostic.column != test->column) {
        fprintf(stderr, "limits adversarial test [%s]: expected column %u, got %u\n",
                test->name, test->column, diagnostic.column);
        failures++;
    }
}

static void expect_acceptance(const char *name, const char *source) {
    DeusProgram program;
    DeusDiagnostic diagnostic;
    if (!compile_source(source, &program, &diagnostic)) {
        fprintf(stderr, "limits adversarial test [%s]: unexpected %u:%u: %s\n",
                name, diagnostic.line, diagnostic.column, diagnostic.message);
        failures++;
        return;
    }
    deus_program_free(&program);
}

static int programs_equal(const DeusProgram *left, const DeusProgram *right) {
    uint32_t index;
    if (left->code_count != right->code_count ||
        left->string_count != right->string_count) return 0;
    for (index = 0u; index < left->code_count; index++) {
        if (left->code[index].opcode != right->code[index].opcode ||
            left->code[index].operand != right->code[index].operand ||
            left->code[index].immediate != right->code[index].immediate) return 0;
    }
    for (index = 0u; index < left->string_count; index++) {
        if (left->strings[index].len != right->strings[index].len ||
            memcmp(left->strings[index].data, right->strings[index].data,
                   left->strings[index].len)) return 0;
    }
    return 1;
}

static void test_rejections(void) {
    static const Rejection tests[] = {
        {"short flow indent", "flow main:\n  limits:\n        workers 1\n", "multiples of four", 2u, 3u},
        {"short entry indent", "flow main:\n    limits:\n      workers 1\n", "multiples of four", 3u, 7u},
        {"tab flow indent", "flow main:\n\tlimits:\n        workers 1\n", "tabs are not allowed", 2u, 1u},
        {"tab entry indent", "flow main:\n    limits:\n\t\tworkers 1\n", "tabs are not allowed", 3u, 1u},
        {"mixed indent", "flow main:\n    limits:\n        \tworkers 1\n", "tabs are not allowed", 3u, 9u},
        {"empty block at eof", "flow main:\n    limits:", "expected an indented limits entry", 3u, 1u},
        {"comment-only block", "flow main:\n    limits:\n        # none\n", "expected an indented limits entry", 3u, 1u},
        {"blank-only block", "flow main:\n    limits:\n\n", "expected an indented limits entry", 3u, 1u},
        {"duplicate workers", "flow main:\n    limits:\n        workers 1\n        workers 2\n", "duplicate `workers` entry", 4u, 9u},
        {"unknown entry", "flow main:\n    limits:\n        timeout 10\n", "unknown limit `timeout`", 3u, 9u},
        {"unknown suggestion", "flow main:\n    limits:\n        workerss 1\n", "did you mean `workers`", 3u, 9u},
        {"missing value", "flow main:\n    limits:\n        retry\n", "expected a limit name and unsigned integer", 3u, 9u},
        {"negative value", "flow main:\n    limits:\n        retry -1\n", "unsigned integer literal", 3u, 15u},
        {"signed positive", "flow main:\n    limits:\n        retry +1\n", "unsigned integer literal", 3u, 15u},
        {"decimal value", "flow main:\n    limits:\n        rate 1.5\n", "unsigned integer literal", 3u, 15u},
        {"u32 overflow", "flow main:\n    limits:\n        workers 4294967296\n", "exceeds U32", 3u, 9u},
        {"huge overflow", "flow main:\n    limits:\n        rate 999999999999999999999999999999999999999999\n", "exceeds U32", 3u, 9u},
        {"workers zero", "flow main:\n    limits:\n        workers 0\n", "limit must be between 1 and 256", 3u, 17u},
        {"workers high", "flow main:\n    limits:\n        workers 257\n", "limit must be between 1 and 256", 3u, 17u},
        {"retry high", "flow main:\n    limits:\n        retry 17\n", "retry must be at most 16", 3u, 15u},
        {"backoff high", "flow main:\n    limits:\n        backoff 60001\n", "backoff must be at most 60000 ms", 3u, 17u},
        {"rate high", "flow main:\n    limits:\n        rate 10001\n", "rate must be at most 10000 rps", 3u, 14u},
        {"nested garbage", "flow main:\n    limits:\n        workers 1\n            retry 1\n", "limits entries must be indented by eight spaces", 4u, 1u},
        {"indent jump", "flow main:\n            limits:\n                workers 1\n", "flow items must be indented by four spaces", 2u, 1u},
        {"unexpected dedent", "flow main:\n    limits:\n        workers 1\nretry 1\n", "unexpected top-level content", 4u, 1u},
        {"multiple blocks", "flow main:\n    limits:\n        workers 1\n    limits:\n        retry 1\n", "multiple `limits` blocks", 4u, 5u},
        {"limits outside flow", "limits:\n    workers 1\n", "unknown instruction", 1u, 1u},
        {"late after hunt", "flow main:\n    hunt \"https://example.test\"\n    limits:\n        workers 1\n", "network execution", 4u, 17u},
        {"late after fork", "flow main:\n    fork \"https://example.test\"\n    limits:\n        retry 1\n", "network execution", 4u, 15u},
        {"mixed workers legacy first", "flow main:\n    limit 2\n    limits:\n        workers 3\n", "cannot appear both", 3u, 5u},
        {"mixed retry modern first", "flow main:\n    limits:\n        retry 2\n    retry 3\n", "cannot appear both", 4u, 5u},
        {"explicit genesis", "flow main:\n    genesis\n", "explicit `genesis` and `halt` are not allowed", 2u, 5u},
        {"explicit halt", "flow main:\n    halt\n", "explicit `genesis` and `halt` are not allowed", 2u, 5u},
        {"malformed header", "flow main:\n    limits :\n        workers 1\n", "expected `limits:`", 2u, 5u},
        {"trailing garbage", "flow main:\n    limits:\n        workers 1 garbage\n", "unsigned integer literal", 3u, 18u}
    };
    size_t index;
    for (index = 0u; index < sizeof(tests) / sizeof(tests[0]); index++)
        expect_rejection(&tests[index]);
}

static void test_acceptance_and_equivalence(void) {
    static const char modern[] =
        "flow main:\n"
        "    limits:\n"
        "        rate 20\n"
        "        backoff 100\n"
        "        retry 3\n"
        "        workers 8\n"
        "    bind answer = 42\n";
    static const char legacy[] =
        "genesis\nlimit 8\nretry 3\nbackoff 100\nrate 20\n"
        "bind answer = 42\nhalt\n";
    static const char comments_and_crlf[] =
        "# preface\r\nflow main:\r\n    limits:\r\n"
        "        # executor budget\r\n        workers 1\r\n\r\n"
        "        // no retry\r\n        retry 0\r\n    bind answer = 42\r\n";
    DeusProgram modern_program;
    DeusProgram legacy_program;
    DeusDiagnostic diagnostic;
    int modern_ok = compile_source(modern, &modern_program, &diagnostic);
    if (!modern_ok) {
        fail("modern equivalence", diagnostic.message);
        return;
    }
    if (!compile_source(legacy, &legacy_program, &diagnostic)) {
        deus_program_free(&modern_program);
        fail("legacy equivalence", diagnostic.message);
        return;
    }
    if (!programs_equal(&modern_program, &legacy_program))
        fail("bytecode equivalence", "modern and legacy programs differ");
    deus_program_free(&modern_program);
    deus_program_free(&legacy_program);
    expect_acceptance("comments blank lines and CRLF", comments_and_crlf);
    expect_acceptance("partial workers", "flow main:\n    limits:\n        workers 1\n");
    expect_acceptance("partial retry zero", "flow main:\n    limits:\n        retry 0\n");
    expect_acceptance("boundary maximums",
                      "flow main:\n    limits:\n        workers 256\n        retry 16\n        backoff 60000\n        rate 10000\n");
}

static void test_deterministic_layout_mutations(void) {
    static const Rejection mutations[] = {
        {"mutation header indented", " flow main:\n    limits:\n        workers 1\n", "indentation must use multiples of four spaces", 1u, 2u},
        {"mutation body dedented", "flow main:\nlimits:\n        workers 1\n", "unexpected top-level content", 2u, 1u},
        {"mutation entry dedented", "flow main:\n    limits:\n    workers 1\n", "expected an indented limits entry", 3u, 1u},
        {"mutation entry overindented", "flow main:\n    limits:\n            workers 1\n", "limits entries must be indented by eight spaces", 3u, 1u},
        {"mutation colon removed", "flow main:\n    limits\n        workers 1\n", "expected `limits:`", 2u, 5u},
        {"mutation key case", "flow main:\n    limits:\n        Workers 1\n", "unknown limit `Workers`", 3u, 9u},
        {"mutation value suffix", "flow main:\n    limits:\n        workers 1u\n", "unsigned integer literal", 3u, 18u},
        {"mutation extra level after blank", "flow main:\n    limits:\n\n            workers 1\n", "limits entries must be indented by eight spaces", 4u, 1u}
    };
    size_t index;
    for (index = 0u; index < sizeof(mutations) / sizeof(mutations[0]); index++)
        expect_rejection(&mutations[index]);
}

int main(void) {
    test_rejections();
    test_acceptance_and_equivalence();
    test_deterministic_layout_mutations();
    return failures ? 1 : 0;
}
