#include "deus.h"

#include <stdio.h>
#include <string.h>

typedef struct { unsigned calls; } State;

static int adapter_call(void *context, const char *adapter, size_t adapter_length,
                        const DeusValue *input, DeusValueContext *values,
                        DeusValue *output, char *error, size_t error_cap) {
    State *state = (State *)context;
    size_t length = 0u;
    const char *text = (const char *)deus_value_data(input, &length);
    (void)error; (void)error_cap;
    if (input->kind != DEUS_VALUE_STRING || length != 5u || memcmp(text, "EDEN!", 5u)) return 0;
    state->calls++;
    if (adapter_length == 11u && !memcmp(adapter, "demo.record", 11u)) {
        DeusValue match = {0};
        if (!deus_value_record(values, output) || !deus_value_string(values, "matched", 7u, &match) ||
            !deus_value_record_set(output, "best_match", 10u, &match)) {
            deus_value_dispose(&match);
            deus_value_dispose(output);
            return 0;
        }
        deus_value_dispose(&match);
        return 1;
    }
    return adapter_length == 11u && !memcmp(adapter, "demo.scalar", 11u) &&
           deus_value_string(values, "not-a-record", 12u, output);
}

static int run_case(const char *source, const char *expected, unsigned expected_calls) {
    DeusProgram program = {0}; DeusDiagnostic diagnostic = {0}; State state = {0};
    DeusHost host = {DEUS_HOST_ABI_VERSION, DEUS_HOST_CAP_ADAPTER_CALL, &state, NULL, NULL, adapter_call};
    FILE *output = tmpfile(); char text[32] = {0}; size_t expected_length = strlen(expected);
    if (!output || !deus_parse_source(source, strlen(source), &program, &diagnostic) ||
        deus_vm_execute_program_with_host(&program, output, &host) || state.calls != expected_calls) return 0;
    rewind(output);
    if (fread(text, 1u, sizeof(text) - 1u, output) != expected_length || memcmp(text, expected, expected_length)) return 0;
    fclose(output); deus_program_free(&program); return 1;
}

int main(void) {
    const char *record = "genesis\nbind query = \"EDEN!\"\nbind result = call \"demo.record\" query\nbind match = get result \"best_match\"\nload match\nemit\nhalt\n";
    const char *optional = "genesis\nbind query = \"EDEN!\"\nbind result = call \"demo.scalar\" query\nbind match = get? result \"best_match\"\nload match\nemit\nhalt\n";
    return run_case(record, "matched", 1u) && run_case(optional, "null", 1u) ? 0 : 1;
}
