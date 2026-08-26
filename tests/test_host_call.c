#include "deus.h"

#include <stdio.h>
#include <string.h>

typedef struct { unsigned calls; } State;

static int echo_call(void *context, const char *adapter, size_t adapter_length,
                     const DeusValue *input, DeusValueContext *values,
                     DeusValue *output, char *error, size_t error_cap) {
    State *state = (State *)context;
    size_t length = 0u; const char *text = (const char *)deus_value_data(input, &length);
    (void)error; (void)error_cap;
    if (adapter_length != 9u || memcmp(adapter, "demo.echo", 9u) ||
        input->kind != DEUS_VALUE_STRING || length != 5u || memcmp(text, "EDEN!", 5u)) return 0;
    state->calls++;
    return deus_value_string(values, "matched", 7u, output);
}

int main(void) {
    const char *source = "genesis\nbind query = \"EDEN!\"\nbind result = call \"demo.echo\" query\nload result\nemit\nhalt\n";
    DeusProgram program = {0}; DeusDiagnostic diagnostic = {0}; State state = {0};
    DeusHost host = {DEUS_HOST_ABI_VERSION, DEUS_HOST_CAP_ADAPTER_CALL,
                     &state, NULL, NULL, echo_call};
    FILE *output = tmpfile(); char text[16] = {0};
    if (!output || !deus_parse_source(source, strlen(source), &program, &diagnostic) ||
        deus_vm_execute_program_with_host(&program, output, &host) || state.calls != 1u) return 1;
    rewind(output); if (fread(text, 1u, sizeof(text) - 1u, output) != 7u || memcmp(text, "matched", 7u)) return 1;
    fclose(output); deus_program_free(&program); return 0;
}
