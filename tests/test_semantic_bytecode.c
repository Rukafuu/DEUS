#include "deus.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__, __LINE__, #condition); \
    exit(1); \
} } while (0)

static void reject(const char *name, const DeusInstruction *code, uint32_t count,
                   const DeusString *strings, uint32_t string_count,
                   const char *expected) {
    DeusProgram program = {(DeusString *)strings, string_count,
                           (DeusInstruction *)code, count};
    char error[192] = {0};
    if (deus_validate_program(&program, error, sizeof(error)) ||
        !strstr(error, expected)) {
        fprintf(stderr, "%s: expected rejection containing '%s', got '%s'\n",
                name, expected, error);
        exit(1);
    }
}
static void reject_loaded(const DeusInstruction *code, uint32_t count,
                          const DeusString *strings, uint32_t string_count,
                          const char *expected) {
    const char *path = "deus_semantic_invalid.deusb";
    DeusProgram program = {(DeusString *)strings, string_count,
                           (DeusInstruction *)code, count};
    DeusProgram decoded;
    char error[192] = {0};
    CHECK(deus_write_binary(&program, path, error, sizeof(error)));
    if (deus_read_binary(path, &decoded, error, sizeof(error)) ||
        !strstr(error, expected)) {
        fprintf(stderr, "loader accepted invalid bytecode or returned '%s'\n", error);
        exit(1);
    }
    CHECK(!remove(path));
}


int main(void) {
    static const DeusString strings[] = {
        {(char *)"net.http2", 9u}, {(char *)"http://invalid.test", 19u}
    };
    static const DeusInstruction duplicate_genesis[] = {
        {DEUS_GENESIS, 0u, 0}, {DEUS_GENESIS, 0u, 0}, {DEUS_HALT, 0u, 0}
    };
    static const DeusInstruction after_halt[] = {
        {DEUS_GENESIS, 0u, 0}, {DEUS_HALT, 0u, 0}, {DEUS_CONST_NULL, 0u, 0}
    };
    static const DeusInstruction stack_underflow[] = {
        {DEUS_GENESIS, 0u, 0}, {DEUS_EMIT, 0u, 0}, {DEUS_HALT, 0u, 0}
    };
    static const DeusInstruction wrong_future[] = {
        {DEUS_GENESIS, 0u, 0}, {DEUS_CONST_NULL, 0u, 0},
        {DEUS_AWAIT, 0u, 0}, {DEUS_HALT, 0u, 0}
    };
    static const DeusInstruction unresolved_future[] = {
        {DEUS_GENESIS, 0u, 0}, {DEUS_FORK, 1u, 0}, {DEUS_HALT, 0u, 0}
    };
    static const DeusInstruction invalid_limit[] = {
        {DEUS_GENESIS, 0u, 0}, {DEUS_LIMIT, 0u, 0}, {DEUS_HALT, 0u, 0}
    };
    static const DeusInstruction late_retry[] = {
        {DEUS_GENESIS, 0u, 0}, {DEUS_HUNT, 1u, 0},
        {DEUS_RETRY, 1u, 0}, {DEUS_HALT, 0u, 0}
    };
    static const DeusInstruction valid[] = {
        {DEUS_OMNI, 0u, 0}, {DEUS_GENESIS, 0u, 0}, {DEUS_LIMIT, 2u, 0},
        {DEUS_FORK, 1u, 0}, {DEUS_AWAIT, 0u, 0}, {DEUS_REAP, 0u, 0},
        {DEUS_EMIT, 0u, 0}, {DEUS_HALT, 0u, 0}
    };
    DeusProgram program = {(DeusString *)strings, 2u, (DeusInstruction *)valid,
                           (uint32_t)(sizeof(valid) / sizeof(valid[0]))};
    char error[192] = {0};

    reject("duplicate genesis", duplicate_genesis, 3u, strings, 2u,
           "GENESIS may appear only once");
    reject("instruction after halt", after_halt, 3u, strings, 2u,
           "instruction after HALT");
    reject("stack underflow", stack_underflow, 3u, strings, 2u,
           "EMIT requires");
    reject("await non-future", wrong_future, 4u, strings, 2u,
           "AWAIT requires a future");
    reject("unresolved future", unresolved_future, 3u, strings, 2u,
           "unresolved future");
    reject("zero executor limit", invalid_limit, 3u, strings, 2u,
           "LIMIT must be between");
    reject("late executor setting", late_retry, 4u, strings, 2u,
           "executor configuration after network");
    reject_loaded(invalid_limit, 3u, strings, 2u, "LIMIT must be between");

    CHECK(deus_validate_program(&program, error, sizeof(error)));
    puts("semantic bytecode validation passed");
    return 0;
}
