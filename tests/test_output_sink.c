#include "deus.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    char data[256];
    size_t length;
    size_t writes;
    int reject;
} Buffer;

static int buffer_write(void *context, const void *data, size_t length) {
    Buffer *buffer = (Buffer *)context;
    if (buffer->reject || length > sizeof(buffer->data) - buffer->length)
        return 0;
    memcpy(buffer->data + buffer->length, data, length);
    buffer->length += length;
    buffer->writes++;
    return 1;
}

static int compile(DeusProgram *program) {
    static const char source[] =
        "genesis\n"
        "bind result = {\"title\": \"Frieren\", \"year\": 2023}\n"
        "load result\n"
        "emit\n"
        "halt\n";
    DeusDiagnostic diagnostic = {0};
    if (!deus_parse_source(source, sizeof(source) - 1u, program, &diagnostic)) {
        fprintf(stderr, "output sink compile failed at %u:%u: %s\n",
                diagnostic.line, diagnostic.column, diagnostic.message);
        return 0;
    }
    return 1;
}

int main(void) {
    static const char expected[] = "{\"title\":\"Frieren\",\"year\":2023}";
    DeusProgram program = {0};
    Buffer buffer = {0};
    DeusOutputSink sink = {DEUS_OUTPUT_ABI_VERSION, &buffer, buffer_write};
    DeusOutputSink invalid = sink;
    Buffer rejecting = {0};
    DeusOutputSink rejected = {DEUS_OUTPUT_ABI_VERSION, &rejecting, buffer_write};

    if (!compile(&program)) return 1;
    if (deus_vm_execute_program_with_sink(&program, &sink, NULL) ||
        buffer.length != sizeof(expected) - 1u ||
        memcmp(buffer.data, expected, sizeof(expected) - 1u) != 0 ||
        buffer.writes == 0u) {
        fprintf(stderr, "output sink did not capture the expected JSON\n");
        deus_program_free(&program);
        return 1;
    }

    invalid.abi_version++;
    if (!deus_vm_execute_program_with_sink(&program, &invalid, NULL)) {
        fprintf(stderr, "output sink accepted an unsupported ABI version\n");
        deus_program_free(&program);
        return 1;
    }

    rejecting.reject = 1;
    if (!deus_vm_execute_program_with_sink(&program, &rejected, NULL)) {
        fprintf(stderr, "output sink rejection did not stop execution\n");
        deus_program_free(&program);
        return 1;
    }

    deus_program_free(&program);
    puts("embeddable output sink passed");
    return 0;
}
