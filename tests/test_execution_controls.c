#include "deus.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    char output[32];
    size_t length;
    uint64_t now;
    int cancelled;
} State;

static int write_output(void *context, const void *data, size_t length) {
    State *state = (State *)context;
    if (length > sizeof(state->output) - state->length) return 0;
    memcpy(state->output + state->length, data, length);
    state->length += length;
    return 1;
}

static uint64_t now_ms(void *context) {
    return ((State *)context)->now;
}

static int should_cancel(void *context) {
    return ((State *)context)->cancelled;
}

static int run(const DeusProgram *program, State *state,
               DeusExecutionOptions *options) {
    DeusOutputSink sink = {DEUS_OUTPUT_ABI_VERSION, state, write_output};
    options->context = state;
    return deus_vm_execute_program_with_options(program, &sink, NULL, options);
}

int main(void) {
    DeusInstruction code[] = {
        {DEUS_GENESIS, 0u, 0},
        {DEUS_CONST_I64, 0u, 42},
        {DEUS_EMIT, 0u, 0},
        {DEUS_HALT, 0u, 0}
    };
    DeusProgram program = {NULL, 0u, code, 4u};
    DeusExecutionOptions options = deus_execution_options_default();
    State state = {0};

    if (options.abi_version != DEUS_EXECUTION_ABI_VERSION ||
        options.instruction_limit != DEUS_DEFAULT_INSTRUCTION_LIMIT ||
        run(&program, &state, &options) || state.length != 2u ||
        memcmp(state.output, "42", 2u)) {
        fprintf(stderr, "default execution options failed\n");
        return 1;
    }

    memset(&state, 0, sizeof(state));
    options = deus_execution_options_default();
    options.instruction_limit = 2u;
    if (!run(&program, &state, &options) || state.length != 0u) {
        fprintf(stderr, "instruction budget did not stop before EMIT\n");
        return 1;
    }

    memset(&state, 0, sizeof(state));
    state.cancelled = 1;
    options = deus_execution_options_default();
    options.should_cancel = should_cancel;
    if (!run(&program, &state, &options) || state.length != 0u) {
        fprintf(stderr, "cancellation did not stop execution\n");
        return 1;
    }

    memset(&state, 0, sizeof(state));
    state.now = 100u;
    options = deus_execution_options_default();
    options.deadline_ms = 100u;
    options.now_ms = now_ms;
    if (!run(&program, &state, &options) || state.length != 0u) {
        fprintf(stderr, "deadline did not stop execution\n");
        return 1;
    }

    memset(&state, 0, sizeof(state));
    options = deus_execution_options_default();
    options.deadline_ms = 100u;
    if (!run(&program, &state, &options)) {
        fprintf(stderr, "deadline without a clock was accepted\n");
        return 1;
    }

    puts("cooperative execution controls passed");
    return 0;
}
