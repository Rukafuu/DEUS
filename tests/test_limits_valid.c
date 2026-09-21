#include "deus.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition, message)                                                \
    do {                                                                         \
        if (!(condition)) {                                                      \
            fprintf(stderr, "limits valid test: %s (line %d)\n", message,       \
                    __LINE__);                                                   \
            return 0;                                                            \
        }                                                                        \
    } while (0)

typedef struct {
    uint8_t opcode;
    uint32_t operand;
} ExpectedInstruction;

static int compile_and_expect(const char *source,
                              const ExpectedInstruction *expected,
                              size_t expected_count,
                              const char *case_name) {
    DeusProgram program = {0};
    DeusDiagnostic diagnostic = {0};
    size_t index;

    if (!deus_parse_source(source, strlen(source), &program, &diagnostic)) {
        fprintf(stderr,
                "limits valid test: %s failed to compile at %u:%u: %s\n",
                case_name, diagnostic.line, diagnostic.column,
                diagnostic.message);
        return 0;
    }
    if ((size_t)program.code_count != expected_count) {
        fprintf(stderr,
                "limits valid test: %s produced %u instructions, expected %zu\n",
                case_name, program.code_count, expected_count);
        deus_program_free(&program);
        return 0;
    }
    for (index = 0u; index < expected_count; index++) {
        if (program.code[index].opcode != expected[index].opcode ||
            program.code[index].operand != expected[index].operand) {
            fprintf(stderr,
                    "limits valid test: %s instruction %zu was (%u, %u), "
                    "expected (%u, %u)\n",
                    case_name, index, (unsigned)program.code[index].opcode,
                    program.code[index].operand,
                    (unsigned)expected[index].opcode,
                    expected[index].operand);
            deus_program_free(&program);
            return 0;
        }
    }
    deus_program_free(&program);
    return 1;
}

static int test_subsets(void) {
    static const struct {
        const char *entry;
        uint8_t opcode;
        uint32_t operand;
        const char *name;
    } cases[] = {
        {"workers 8", DEUS_LIMIT, 8u, "workers subset"},
        {"retry 3", DEUS_RETRY, 3u, "retry subset"},
        {"backoff 100", DEUS_BACKOFF, 100u, "backoff subset"},
        {"rate 20", DEUS_RATE, 20u, "rate subset"}
    };
    size_t index;

    for (index = 0u; index < sizeof(cases) / sizeof(cases[0]); index++) {
        char source[128];
        ExpectedInstruction expected[] = {
            {DEUS_GENESIS, 0u},
            {cases[index].opcode, cases[index].operand},
            {DEUS_HALT, 0u}
        };
        int written = snprintf(source, sizeof(source),
                               "flow main:\n    limits:\n        %s\n",
                               cases[index].entry);
        CHECK(written > 0 && (size_t)written < sizeof(source),
              "subset source construction");
        CHECK(compile_and_expect(source, expected,
                                 sizeof(expected) / sizeof(expected[0]),
                                 cases[index].name),
              "single-entry limits subset");
    }
    return 1;
}

static int test_all_entries_and_canonical_order(void) {
    const char *source =
        "flow crawl:\n"
        "    limits:\n"
        "        rate 20\n"
        "        backoff 100\n"
        "        workers 8\n"
        "        retry 3\n";
    const ExpectedInstruction expected[] = {
        {DEUS_GENESIS, 0u},
        {DEUS_LIMIT, 8u},
        {DEUS_RETRY, 3u},
        {DEUS_BACKOFF, 100u},
        {DEUS_RATE, 20u},
        {DEUS_HALT, 0u}
    };
    CHECK(compile_and_expect(source, expected,
                             sizeof(expected) / sizeof(expected[0]),
                             "all entries in varied order"),
          "canonical lowering order");
    return 1;
}

static int test_boundaries(void) {
    const char *minimums =
        "flow minimums:\n"
        "    limits:\n"
        "        workers 1\n"
        "        retry 0\n"
        "        backoff 0\n"
        "        rate 0\n";
    const char *maximums =
        "flow maximums:\n"
        "    limits:\n"
        "        workers 256\n"
        "        retry 16\n"
        "        backoff 60000\n"
        "        rate 10000\n";
    const ExpectedInstruction minimum_expected[] = {
        {DEUS_GENESIS, 0u}, {DEUS_LIMIT, 1u}, {DEUS_RETRY, 0u},
        {DEUS_BACKOFF, 0u}, {DEUS_RATE, 0u}, {DEUS_HALT, 0u}
    };
    const ExpectedInstruction maximum_expected[] = {
        {DEUS_GENESIS, 0u}, {DEUS_LIMIT, 256u}, {DEUS_RETRY, 16u},
        {DEUS_BACKOFF, 60000u}, {DEUS_RATE, 10000u}, {DEUS_HALT, 0u}
    };
    CHECK(compile_and_expect(minimums, minimum_expected,
                             sizeof(minimum_expected) /
                                 sizeof(minimum_expected[0]),
                             "minimum boundaries"),
          "minimum boundary values");
    CHECK(compile_and_expect(maximums, maximum_expected,
                             sizeof(maximum_expected) /
                                 sizeof(maximum_expected[0]),
                             "maximum boundaries"),
          "maximum boundary values");
    return 1;
}

static int test_comments_blanks_and_following_statements(void) {
    const char *source =
        "# comments before a modern flow are ignored\n"
        "flow main:\n"
        "    limits:\n"
        "        # executor capacity\n"
        "        workers 4\n"
        "\n"
        "        // transient failures\n"
        "        retry 2\n"
        "\n"
        "    bind answer = 42\n"
        "    load answer\n"
        "    emit\n";
    const ExpectedInstruction expected[] = {
        {DEUS_GENESIS, 0u},
        {DEUS_LIMIT, 4u},
        {DEUS_RETRY, 2u},
        {DEUS_CONST_I64, 0u},
        {DEUS_BIND, 0u},
        {DEUS_LOAD, 0u},
        {DEUS_EMIT, 0u},
        {DEUS_HALT, 0u}
    };
    DeusProgram program = {0};
    DeusDiagnostic diagnostic = {0};

    CHECK(compile_and_expect(source, expected,
                             sizeof(expected) / sizeof(expected[0]),
                             "comments blanks and statements"),
          "modern central instruction sequence");
    CHECK(deus_parse_source(source, strlen(source), &program, &diagnostic),
          "compile following statement immediates");
    CHECK(program.code[3].immediate == 42,
          "statement after limits keeps its I64 immediate");
    CHECK(program.string_count == 0u,
          "local-only sequence does not create an unrelated string constant");
    deus_program_free(&program);
    return 1;
}

static int test_multiple_structured_literal_fragments(void) {
    const char *source =
        "flow main:\n"
        "    bind first = {\n"
        "        \"a\": 1\n"
        "    }\n"
        "    bind second = {\n"
        "        \"b\": 2\n"
        "    }\n";
    DeusProgram program = {0};
    DeusDiagnostic diagnostic = {0};
    CHECK(deus_parse_source(source, strlen(source), &program, &diagnostic),
          "separate raw fragments use distinct internal literal symbols");
    deus_program_free(&program);
    return 1;
}

int main(void) {
    if (!test_subsets()) return 1;
    if (!test_all_entries_and_canonical_order()) return 1;
    if (!test_boundaries()) return 1;
    if (!test_comments_blanks_and_following_statements()) return 1;
    if (!test_multiple_structured_literal_fragments()) return 1;
    return 0;
}
