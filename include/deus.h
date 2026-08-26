#ifndef DEUS_H
#define DEUS_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define DEUS_HOST_ABI_VERSION 1u
#define DEUS_OUTPUT_ABI_VERSION 1u
#define DEUS_EXECUTION_ABI_VERSION 1u
#define DEUS_DEFAULT_INSTRUCTION_LIMIT UINT64_C(1000000)
#define DEUS_ABI_VERSION 1u
#define DEUS_HEADER_SIZE 40u
#define DEUS_MAX_SECTION (64u * 1024u * 1024u)
#define DEUS_MAX_STRINGS 1000000u
#define DEUS_MAX_LOCALS 256u

typedef enum {
    DEUS_OMNI = 0x01, DEUS_GENESIS = 0x02, DEUS_HUNT = 0x03,
    DEUS_REAP = 0x04, DEUS_HALT = 0x05, DEUS_EMIT = 0x06,
    DEUS_FORK = 0x07, DEUS_AWAIT = 0x08, DEUS_JOIN = 0x09,
    DEUS_LIMIT = 0x0A, DEUS_RETRY = 0x0B, DEUS_BACKOFF = 0x0C,
    DEUS_RATE = 0x0D, DEUS_CONST = 0x0E, DEUS_BIND = 0x0F,
    DEUS_LOAD = 0x10, DEUS_CONST_NULL = 0x11, DEUS_CONST_BOOL = 0x12,
    DEUS_CONST_I64 = 0x13, DEUS_URL_ENCODE = 0x14, DEUS_URL_JOIN = 0x15,
    DEUS_HUNT_VALUE = 0x16, DEUS_JSON_PATH = 0x17,
    DEUS_CONST_RECORD = 0x18, DEUS_CONST_LIST = 0x19,
    DEUS_RECORD_SET = 0x1A, DEUS_LIST_PUSH = 0x1B,
    DEUS_RECORD_GET = 0x1C, DEUS_LIST_AT = 0x1D,
    DEUS_RECORD_GET_OPTIONAL = 0x1E, DEUS_LIST_AT_OPTIONAL = 0x1F,
    DEUS_EQUAL = 0x20, DEUS_NOT_EQUAL = 0x21, DEUS_LESS = 0x22,
    DEUS_LESS_EQUAL = 0x23, DEUS_GREATER = 0x24, DEUS_GREATER_EQUAL = 0x25,
    DEUS_BOOL_AND = 0x26, DEUS_BOOL_OR = 0x27, DEUS_BOOL_NOT = 0x28,
    DEUS_COALESCE = 0x29, DEUS_TO_TEXT = 0x2A, DEUS_TO_I64 = 0x2B,
    DEUS_TO_BOOL = 0x2C
} DeusOpcode;

typedef struct { char *data; uint32_t len; } DeusString;
typedef struct { uint8_t opcode; uint32_t operand; int64_t immediate; } DeusInstruction;
typedef struct {
    DeusString *strings; uint32_t string_count;
    DeusInstruction *code; uint32_t code_count;
} DeusProgram;
typedef struct { unsigned line, column; char message[192]; } DeusDiagnostic;

enum {
    DEUS_HOST_CAP_NETWORK = 1u << 0
};

typedef struct {
    const void *data;
    size_t length;
    uint32_t status;
    void *token;
} DeusHostDocument;

typedef struct {
    uint32_t abi_version;
    uint64_t capabilities;
    void *context;
    int (*hunt)(void *context, const char *url, size_t url_length,
                DeusHostDocument *document, char *error, size_t error_cap);
    void (*release_document)(void *context, DeusHostDocument *document);
} DeusHost;

typedef int (*DeusOutputWrite)(void *context, const void *data, size_t length);

typedef struct {
    uint32_t abi_version;
    void *context;
    DeusOutputWrite write;
} DeusOutputSink;

typedef uint64_t (*DeusExecutionNowMs)(void *context);
typedef int (*DeusExecutionShouldCancel)(void *context);

typedef struct {
    uint32_t abi_version;
    uint64_t instruction_limit;
    uint64_t deadline_ms;
    void *context;
    DeusExecutionNowMs now_ms;
    DeusExecutionShouldCancel should_cancel;
} DeusExecutionOptions;

int deus_host_validate(const DeusHost *host, uint64_t required_capabilities,
                       char *error, size_t error_cap);
DeusExecutionOptions deus_execution_options_default(void);
void deus_program_free(DeusProgram *program);
int deus_parse_source(const char *source, size_t length, DeusProgram *out,
                      DeusDiagnostic *diagnostic);
int deus_write_binary(const DeusProgram *program, const char *path,
                      char *error, size_t error_cap);
int deus_read_binary(const char *path, DeusProgram *out,
                     char *error, size_t error_cap);
int deus_vm_execute_program(const DeusProgram *program, FILE *output);
int deus_validate_program(const DeusProgram *program,
                          char *error, size_t error_cap);
int deus_vm_execute_program_with_host(const DeusProgram *program, FILE *output,
                                      const DeusHost *host);
int deus_vm_execute_program_with_sink(const DeusProgram *program,
                                      const DeusOutputSink *output,
                                      const DeusHost *host);
int deus_vm_execute_program_with_options(const DeusProgram *program,
                                         const DeusOutputSink *output,
                                         const DeusHost *host,
                                         const DeusExecutionOptions *options);

#endif
