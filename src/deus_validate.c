#include "deus.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define VERIFY_STACK_MAX 1024u

enum {
    TYPE_DOCUMENT = 1u << 0, TYPE_TEXT = 1u << 1, TYPE_STRING = 1u << 2,
    TYPE_NULL = 1u << 3, TYPE_BOOL = 1u << 4, TYPE_I64 = 1u << 5,
    TYPE_RECORD = 1u << 6, TYPE_LIST = 1u << 7, TYPE_FUTURE = 1u << 8
};
#define TYPE_SCALAR (TYPE_TEXT | TYPE_STRING | TYPE_NULL | TYPE_BOOL | TYPE_I64)
#define TYPE_VALUE (TYPE_SCALAR | TYPE_RECORD | TYPE_LIST)

static int invalid(char *error, size_t cap, uint32_t pc, const char *format, ...) {
    va_list args;
    if (cap) {
        int prefix = snprintf(error, cap, "invalid bytecode at instruction %u: ", pc);
        if (prefix >= 0 && (size_t)prefix < cap) {
            va_start(args, format);
            vsnprintf(error + prefix, cap - (size_t)prefix, format, args);
            va_end(args);
        }
    }
    return 0;
}

static int require_top(const uint16_t *stack, uint32_t depth, uint16_t allowed,
                       char *error, size_t cap, uint32_t pc, const char *message) {
    return depth && (stack[depth - 1u] & allowed) ? 1 :
           invalid(error, cap, pc, "%s", message);
}

static int adapter_name_valid(const DeusProgram *program, uint32_t operand) {
    const DeusString *name; uint32_t index;
    if (operand >= program->string_count) return 0;
    name = &program->strings[operand];
    if (!name->len || name->data[0] < 'a' || name->data[0] > 'z') return 0;
    for (index = 1u; index < name->len; index++) {
        char character = name->data[index];
        if (!((character >= 'a' && character <= 'z') ||
              (character >= '0' && character <= '9') || character == '.' || character == '-')) return 0;
    }
    return 1;
}

int deus_validate_program(const DeusProgram *program, char *error, size_t cap) {
    uint16_t stack[VERIFY_STACK_MAX] = {0}, locals[DEUS_MAX_LOCALS] = {0};
    unsigned char bound[DEUS_MAX_LOCALS] = {0};
    uint32_t depth = 0u;
    int began = 0, halted = 0, executor_locked = 0;

    if (!program || (program->string_count && !program->strings) ||
        (program->code_count && !program->code))
        return invalid(error, cap, 0u, "invalid program storage");

    for (uint32_t pc = 0u; pc < program->code_count; pc++) {
        DeusInstruction in = program->code[pc];
        uint16_t left, right;
        if (halted) return invalid(error, cap, pc, "instruction after HALT");

        switch (in.opcode) {
        case DEUS_OMNI:
            if (in.operand >= program->string_count ||
                program->strings[in.operand].len != 9u ||
                memcmp(program->strings[in.operand].data, "net.http2", 9u))
                return invalid(error, cap, pc, "unknown executor module");
            break;
        case DEUS_GENESIS:
            if (began) return invalid(error, cap, pc, "GENESIS may appear only once");
            began = 1;
            break;
        case DEUS_LIMIT:
            if (executor_locked) return invalid(error, cap, pc, "executor configuration after network operation");
            if (!in.operand || in.operand > 256u) return invalid(error, cap, pc, "LIMIT must be between 1 and 256");
            break;
        case DEUS_RETRY:
            if (executor_locked) return invalid(error, cap, pc, "executor configuration after network operation");
            if (in.operand > 16u) return invalid(error, cap, pc, "RETRY must be at most 16");
            break;
        case DEUS_BACKOFF:
            if (executor_locked) return invalid(error, cap, pc, "executor configuration after network operation");
            if (in.operand > 60000u) return invalid(error, cap, pc, "BACKOFF must be at most 60000 ms");
            break;
        case DEUS_RATE:
            if (executor_locked) return invalid(error, cap, pc, "executor configuration after network operation");
            if (in.operand > 10000u) return invalid(error, cap, pc, "RATE must be at most 10000 rps");
            break;
        case DEUS_HUNT:
        case DEUS_FORK:
            if (!began) return invalid(error, cap, pc, "network operation before GENESIS");
            if (depth == VERIFY_STACK_MAX) return invalid(error, cap, pc, "stack overflow");
            executor_locked = 1;
            stack[depth++] = in.opcode == DEUS_FORK ? TYPE_FUTURE : TYPE_DOCUMENT;
            break;
        case DEUS_CONST:
            if (!began) return invalid(error, cap, pc, "CONST before GENESIS");
            if (depth == VERIFY_STACK_MAX) return invalid(error, cap, pc, "stack overflow");
            stack[depth++] = TYPE_STRING;
            break;
        case DEUS_CONST_NULL:
        case DEUS_CONST_BOOL:
        case DEUS_CONST_I64:
        case DEUS_CONST_RECORD:
        case DEUS_CONST_LIST:
            if (!began) return invalid(error, cap, pc, "constant before GENESIS");
            if (depth == VERIFY_STACK_MAX) return invalid(error, cap, pc, "stack overflow");
            stack[depth++] = in.opcode == DEUS_CONST_NULL ? TYPE_NULL :
                             in.opcode == DEUS_CONST_BOOL ? TYPE_BOOL :
                             in.opcode == DEUS_CONST_I64 ? TYPE_I64 :
                             in.opcode == DEUS_CONST_RECORD ? TYPE_RECORD : TYPE_LIST;
            break;
        case DEUS_BIND:
            if (!began || !depth) return invalid(error, cap, pc, "BIND requires a stack value");
            if (in.operand >= DEUS_MAX_LOCALS || bound[in.operand])
                return invalid(error, cap, pc, "invalid or duplicate local binding");
            locals[in.operand] = stack[--depth]; bound[in.operand] = 1u;
            break;
        case DEUS_LOAD:
            if (!began || in.operand >= DEUS_MAX_LOCALS || !bound[in.operand])
                return invalid(error, cap, pc, "LOAD references an unbound local");
            if (depth == VERIFY_STACK_MAX) return invalid(error, cap, pc, "stack overflow");
            stack[depth++] = locals[in.operand];
            break;
        case DEUS_AWAIT:
            if (!require_top(stack, depth, TYPE_FUTURE, error, cap, pc, "AWAIT requires a future")) return 0;
            stack[depth - 1u] = TYPE_DOCUMENT;
            break;
        case DEUS_JOIN:
            if (!in.operand || in.operand > depth) return invalid(error, cap, pc, "JOIN exceeds stack");
            for (uint32_t index = depth - in.operand; index < depth; index++) {
                if (!(stack[index] & TYPE_FUTURE))
                    return invalid(error, cap, pc, "JOIN requires contiguous futures");
                stack[index] = TYPE_DOCUMENT;
            }
            break;
        case DEUS_REAP:
            if (!require_top(stack, depth, TYPE_DOCUMENT, error, cap, pc, "REAP requires a document")) return 0;
            stack[depth - 1u] = TYPE_TEXT;
            break;
        case DEUS_JSON_PATH:
            if (!require_top(stack, depth, TYPE_DOCUMENT, error, cap, pc, "JSON_PATH requires a document")) return 0;
            stack[depth - 1u] = TYPE_SCALAR;
            break;
        case DEUS_RECORD_SET:
        case DEUS_LIST_PUSH:
            if (depth < 2u) return invalid(error, cap, pc, "compound mutation requires two values");
            left = stack[depth - 2u]; right = stack[depth - 1u];
            if (!(left & (in.opcode == DEUS_RECORD_SET ? TYPE_RECORD : TYPE_LIST)) ||
                !(right & TYPE_VALUE))
                return invalid(error, cap, pc, "invalid compound mutation state");
            depth -= 2u;
            break;
        case DEUS_RECORD_GET:
        case DEUS_RECORD_GET_OPTIONAL:
            if (!require_top(stack, depth, TYPE_RECORD, error, cap, pc, "record read requires a record")) return 0;
            stack[depth - 1u] = TYPE_VALUE;
            break;
        case DEUS_LIST_AT:
        case DEUS_LIST_AT_OPTIONAL:
            if (!require_top(stack, depth, TYPE_LIST, error, cap, pc, "list read requires a list")) return 0;
            stack[depth - 1u] = TYPE_VALUE;
            break;
        case DEUS_BOOL_NOT:
            if (!require_top(stack, depth, TYPE_BOOL, error, cap, pc, "BOOL_NOT requires Bool")) return 0;
            stack[depth - 1u] = TYPE_BOOL;
            break;
        case DEUS_BOOL_AND:
        case DEUS_BOOL_OR:
            if (depth < 2u || !(stack[depth - 2u] & TYPE_BOOL) ||
                !(stack[depth - 1u] & TYPE_BOOL))
                return invalid(error, cap, pc, "boolean operation requires two Bool values");
            depth--; stack[depth - 1u] = TYPE_BOOL;
            break;
        case DEUS_LESS:
        case DEUS_LESS_EQUAL:
        case DEUS_GREATER:
        case DEUS_GREATER_EQUAL:
            if (depth < 2u || !(stack[depth - 2u] & TYPE_I64) ||
                !(stack[depth - 1u] & TYPE_I64))
                return invalid(error, cap, pc, "ordering requires two I64 values");
            depth--; stack[depth - 1u] = TYPE_BOOL;
            break;
        case DEUS_EQUAL:
        case DEUS_NOT_EQUAL:
        case DEUS_COALESCE:
            if (depth < 2u) return invalid(error, cap, pc, "binary operation requires two values");
            left = stack[depth - 2u]; right = stack[depth - 1u]; depth--;
            stack[depth - 1u] = in.opcode == DEUS_COALESCE ?
                                (uint16_t)(left | right) : TYPE_BOOL;
            break;
        case DEUS_TO_TEXT:
        case DEUS_TO_I64:
        case DEUS_TO_BOOL:
        case DEUS_URL_ENCODE:
            if (!require_top(stack, depth, TYPE_STRING | TYPE_TEXT | TYPE_BOOL | TYPE_I64,
                             error, cap, pc, "conversion requires a scalar")) return 0;
            stack[depth - 1u] = in.opcode == DEUS_TO_I64 ? TYPE_I64 :
                                in.opcode == DEUS_TO_BOOL ? TYPE_BOOL : TYPE_STRING;
            break;
        case DEUS_URL_JOIN:
            if (depth < 2u || !(stack[depth - 2u] & (TYPE_STRING | TYPE_TEXT)) ||
                !(stack[depth - 1u] & (TYPE_STRING | TYPE_TEXT)))
                return invalid(error, cap, pc, "URL_JOIN requires two strings");
            depth--; stack[depth - 1u] = TYPE_STRING;
            break;
        case DEUS_HUNT_VALUE:
            if (!began || !require_top(stack, depth, TYPE_STRING | TYPE_TEXT,
                                       error, cap, pc, "HUNT_VALUE requires a URL string")) return 0;
            executor_locked = 1; stack[depth - 1u] = TYPE_DOCUMENT;
            break;
        case DEUS_HOST_CALL:
            if (!began || !adapter_name_valid(program, in.operand))
                return invalid(error, cap, pc, "HOST_CALL requires a valid adapter name");
            if (!require_top(stack, depth, TYPE_VALUE, error, cap, pc,
                             "HOST_CALL requires a serializable input value")) return 0;
            executor_locked = 1; stack[depth - 1u] = TYPE_VALUE;
            break;
        case DEUS_EMIT:
            if (!require_top(stack, depth, TYPE_VALUE, error, cap, pc,
                             "EMIT requires a serializable value")) return 0;
            depth--;
            break;
        case DEUS_HALT:
            for (uint32_t index = 0u; index < depth; index++)
                if (stack[index] & TYPE_FUTURE)
                    return invalid(error, cap, pc, "HALT with unresolved future on stack");
            for (uint32_t index = 0u; index < DEUS_MAX_LOCALS; index++)
                if (bound[index] && (locals[index] & TYPE_FUTURE))
                    return invalid(error, cap, pc, "HALT with unresolved future in local");
            halted = 1;
            break;
        default:
            return invalid(error, cap, pc, "unknown opcode 0x%02X", in.opcode);
        }
    }
    if (!began) return invalid(error, cap, program->code_count,
                               "program requires exactly one GENESIS");
    if (!halted) return invalid(error, cap, program->code_count,
                                "program requires terminal HALT");
    return 1;
}
