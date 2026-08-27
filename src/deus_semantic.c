#include "deus_compiler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum { LOCAL_NULL, LOCAL_BOOL, LOCAL_I64, LOCAL_STRING, LOCAL_DOCUMENT,
               LOCAL_SCALAR, LOCAL_RECORD, LOCAL_LIST, LOCAL_VALUE } LocalType;
enum { SEMANTIC_STACK_MAX = 1024u };
typedef struct { const char *name; uint32_t length; LocalType type; } LocalSymbol;

static int type_is_serializable(LocalType type) {
    return type != LOCAL_DOCUMENT;
}

static void semantic_error(DeusDiagnostic *diagnostic, const DeusAstInstruction *instruction,
                           const char *message) {
    diagnostic->line = instruction->line; diagnostic->column = instruction->column;
    snprintf(diagnostic->message, sizeof(diagnostic->message), "%s", message);
}

static int add_code(DeusProgram *program, uint8_t opcode, uint32_t operand) {
    DeusInstruction *next = (DeusInstruction *)realloc(program->code,
        ((size_t)program->code_count + 1u) * sizeof(*next));
    if (!next) return 0;
    program->code = next; program->code[program->code_count++] = (DeusInstruction){opcode, operand, 0}; return 1;
}

static int intern_string(DeusProgram *program, const char *value, uint32_t length, uint32_t *index) {
    DeusString *next; char *copy;
    for (uint32_t current = 0; current < program->string_count; current++) {
        if (program->strings[current].len == length && !memcmp(program->strings[current].data, value, length)) {
            *index = current; return 1;
        }
    }
    if (program->string_count == DEUS_MAX_STRINGS) return 0;
    copy = (char *)malloc((size_t)length + 1u); if (!copy) return 0;
    memcpy(copy, value, length); copy[length] = '\0';
    next = (DeusString *)realloc(program->strings,
        ((size_t)program->string_count + 1u) * sizeof(*next));
    if (!next) { free(copy); return 0; }
    program->strings = next; *index = program->string_count;
    program->strings[program->string_count++] = (DeusString){copy, length}; return 1;
}

static int template_error(DeusDiagnostic *diagnostic, const DeusAstInstruction *instruction,
                          const char *message) {
    semantic_error(diagnostic, instruction, message); return 0;
}

static int adapter_name_valid(const char *name, uint32_t length) {
    uint32_t index;
    if (!length || name[0] < 'a' || name[0] > 'z') return 0;
    for (index = 1u; index < length; index++) {
        char character = name[index];
        if (!((character >= 'a' && character <= 'z') ||
              (character >= '0' && character <= '9') ||
              character == '.' || character == '-')) return 0;
    }
    return 1;
}

static int compile_hunt_template(const DeusAstInstruction *instruction,
                                 const LocalSymbol *locals, uint32_t local_count,
                                 DeusProgram *out, DeusDiagnostic *diagnostic) {
    const char *source = instruction->operand.string; size_t length = instruction->string_length;
    char *segment = (char *)malloc(length + 1u); size_t used = 0u; int transformed = 0, began = 0;
    if (!segment) return template_error(diagnostic, instruction, "out of memory");
    for (size_t index = 0; index < length;) {
        if (source[index] == '{' && index + 1u < length && source[index + 1u] == '{') {
            segment[used++] = '{'; index += 2u; transformed = 1; continue;
        }
        if (source[index] == '}' && index + 1u < length && source[index + 1u] == '}') {
            segment[used++] = '}'; index += 2u; transformed = 1; continue;
        }
        if (source[index] == '}') { free(segment); return template_error(diagnostic, instruction, "unmatched '}' in hunt URL"); }
        if (source[index] != '{') { segment[used++] = source[index++]; continue; }
        size_t close = index + 1u;
        while (close < length && source[close] != '}') close++;
        if (close == length) { free(segment); return template_error(diagnostic, instruction, "unterminated placeholder in hunt URL"); }
        size_t name_start = index + 1u, name_length = close - name_start;
        if (!name_length || !((source[name_start] >= 'A' && source[name_start] <= 'Z') ||
            (source[name_start] >= 'a' && source[name_start] <= 'z') || source[name_start] == '_')) {
            free(segment); return template_error(diagnostic, instruction, "invalid placeholder name in hunt URL");
        }
        for (size_t part = 1u; part < name_length; part++) {
            char c = source[name_start + part];
            if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                  (c >= '0' && c <= '9') || c == '_')) {
                free(segment); return template_error(diagnostic, instruction, "invalid placeholder name in hunt URL");
            }
        }
        uint32_t slot = 0u;
        for (; slot < local_count; slot++)
            if (locals[slot].length == name_length && !memcmp(locals[slot].name, source + name_start, name_length)) break;
        if (slot == local_count) { free(segment); return template_error(diagnostic, instruction, "unknown local in hunt URL placeholder"); }
        if (locals[slot].type != LOCAL_STRING && locals[slot].type != LOCAL_I64 && locals[slot].type != LOCAL_BOOL) {
            free(segment); return template_error(diagnostic, instruction, "hunt URL placeholders require String, I64, or Bool");
        }
        uint32_t string_index;
        if (!intern_string(out, segment, (uint32_t)used, &string_index) ||
            !add_code(out, DEUS_CONST, string_index)) { free(segment); return template_error(diagnostic, instruction, "out of memory"); }
        if (began && !add_code(out, DEUS_URL_JOIN, 0u)) { free(segment); return template_error(diagnostic, instruction, "out of memory"); }
        if (!add_code(out, DEUS_LOAD, slot) || !add_code(out, DEUS_URL_ENCODE, 0u) ||
            !add_code(out, DEUS_URL_JOIN, 0u)) { free(segment); return template_error(diagnostic, instruction, "out of memory"); }
        began = 1; transformed = 1; used = 0u; index = close + 1u;
    }
    if (!transformed) { free(segment); return -1; }
    uint32_t string_index;
    if (!intern_string(out, segment, (uint32_t)used, &string_index) ||
        !add_code(out, DEUS_CONST, string_index) ||
        (began && !add_code(out, DEUS_URL_JOIN, 0u)) || !add_code(out, DEUS_HUNT_VALUE, 0u)) {
        free(segment); return template_error(diagnostic, instruction, "out of memory");
    }
    free(segment); return 1;
}

static int compile_expression(const DeusExpressionNode *expression, const LocalSymbol *locals,
                              uint32_t local_count, DeusProgram *out, LocalType *type,
                              DeusDiagnostic *diagnostic) {
    uint32_t operand = 0u; LocalType left_type, right_type; uint8_t opcode = 0u;
    if (expression->kind == DEUS_EXPRESSION_LITERAL) {
        if (expression->literal_kind == DEUS_AST_EXPRESSION_STRING) {
            *type = LOCAL_STRING;
            return intern_string(out, expression->literal.string, expression->string_length, &operand) && add_code(out, DEUS_CONST, operand);
        }
        if (expression->literal_kind == DEUS_AST_EXPRESSION_I64) {
            *type = LOCAL_I64; if (!add_code(out, DEUS_CONST_I64, 0u)) return 0;
            out->code[out->code_count - 1u].immediate = expression->literal.integer; return 1;
        }
        if (expression->literal_kind == DEUS_AST_EXPRESSION_BOOL) {
            *type = LOCAL_BOOL; return add_code(out, DEUS_CONST_BOOL, expression->literal.boolean ? 1u : 0u);
        }
        *type = LOCAL_NULL; return add_code(out, DEUS_CONST_NULL, 0u);
    }
    if (expression->kind == DEUS_EXPRESSION_LOCAL) {
        uint32_t slot = 0u;
        for (; slot < local_count; slot++) if (locals[slot].length == expression->symbol_length &&
            !memcmp(locals[slot].name, expression->symbol, expression->symbol_length)) break;
        if (slot == local_count) { diagnostic->line = expression->line; diagnostic->column = expression->column;
            snprintf(diagnostic->message, sizeof(diagnostic->message), "unknown local in expression"); return 0; }
        *type = locals[slot].type; return add_code(out, DEUS_LOAD, slot);
    }
    if (expression->kind == DEUS_EXPRESSION_MEMBER_ACCESS) {
        if (!compile_expression(expression->object, locals, local_count, out, &left_type, diagnostic)) return 0;
        if (left_type != LOCAL_RECORD && left_type != LOCAL_VALUE) {
            diagnostic->line = expression->line; diagnostic->column = expression->column;
            snprintf(diagnostic->message, sizeof(diagnostic->message), "member access requires Record or Value"); return 0;
        }
        if (!intern_string(out, expression->symbol, expression->symbol_length, &operand) ||
            !add_code(out, DEUS_RECORD_GET, operand)) return 0;
        *type = LOCAL_VALUE; return 1;
    }
    if (expression->kind == DEUS_EXPRESSION_ITEM_ACCESS) {
        if (expression->accessor->kind != DEUS_EXPRESSION_LITERAL ||
            expression->accessor->literal_kind != DEUS_AST_EXPRESSION_I64 ||
            expression->accessor->literal.integer < 0 ||
            (uint64_t)expression->accessor->literal.integer > UINT32_MAX) {
            diagnostic->line = expression->accessor->line; diagnostic->column = expression->accessor->column;
            snprintf(diagnostic->message, sizeof(diagnostic->message), "item access requires an unsigned integer literal index"); return 0;
        }
        if (!compile_expression(expression->object, locals, local_count, out, &left_type, diagnostic)) return 0;
        if (left_type != LOCAL_LIST && left_type != LOCAL_VALUE) {
            diagnostic->line = expression->line; diagnostic->column = expression->column;
            snprintf(diagnostic->message, sizeof(diagnostic->message), "item access requires List or Value"); return 0;
        }
        if (!add_code(out, DEUS_LIST_AT, (uint32_t)expression->accessor->literal.integer)) return 0;
        *type = LOCAL_VALUE; return 1;
    }
    if (!compile_expression(expression->left, locals, local_count, out, &left_type, diagnostic)) return 0;
    if (expression->kind == DEUS_EXPRESSION_UNARY) {
        if (left_type != LOCAL_BOOL) {
            snprintf(diagnostic->message, sizeof(diagnostic->message), "not requires Bool"); return 0;
        }
        *type = LOCAL_BOOL; return add_code(out, DEUS_BOOL_NOT, 0u);
    }
    if (expression->kind == DEUS_EXPRESSION_CONVERSION) {
        if (left_type == LOCAL_NULL || left_type == LOCAL_DOCUMENT || left_type == LOCAL_RECORD || left_type == LOCAL_LIST) {
            snprintf(diagnostic->message, sizeof(diagnostic->message), "conversion requires String, I64, or Bool"); return 0;
        }
        opcode = expression->operator_kind == DEUS_EXPRESSION_OP_TEXT ? DEUS_TO_TEXT :
                 expression->operator_kind == DEUS_EXPRESSION_OP_I64 ? DEUS_TO_I64 : DEUS_TO_BOOL;
        *type = opcode == DEUS_TO_TEXT ? LOCAL_STRING : opcode == DEUS_TO_I64 ? LOCAL_I64 : LOCAL_BOOL;
        return add_code(out, opcode, 0u);
    }
    if (!compile_expression(expression->right, locals, local_count, out, &right_type, diagnostic)) return 0;
    switch (expression->operator_kind) {
        case DEUS_EXPRESSION_OP_AND: opcode = DEUS_BOOL_AND; break;
        case DEUS_EXPRESSION_OP_OR: opcode = DEUS_BOOL_OR; break;
        case DEUS_EXPRESSION_OP_EQUAL: opcode = DEUS_EQUAL; break;
        case DEUS_EXPRESSION_OP_NOT_EQUAL: opcode = DEUS_NOT_EQUAL; break;
        case DEUS_EXPRESSION_OP_LESS: opcode = DEUS_LESS; break;
        case DEUS_EXPRESSION_OP_LESS_EQUAL: opcode = DEUS_LESS_EQUAL; break;
        case DEUS_EXPRESSION_OP_GREATER: opcode = DEUS_GREATER; break;
        case DEUS_EXPRESSION_OP_GREATER_EQUAL: opcode = DEUS_GREATER_EQUAL; break;
        case DEUS_EXPRESSION_OP_COALESCE: opcode = DEUS_COALESCE; break;
        case DEUS_EXPRESSION_OP_ADD: opcode = DEUS_ADD_I64; break;
        case DEUS_EXPRESSION_OP_SUBTRACT: opcode = DEUS_SUB_I64; break;
        case DEUS_EXPRESSION_OP_MULTIPLY: opcode = DEUS_MUL_I64; break;
        case DEUS_EXPRESSION_OP_DIVIDE: opcode = DEUS_DIV_I64; break;
        case DEUS_EXPRESSION_OP_MODULO: opcode = DEUS_MOD_I64; break;
        default: return 0;
    }
    if (opcode == DEUS_BOOL_AND || opcode == DEUS_BOOL_OR) {
        if ((left_type != LOCAL_BOOL) ||
            (right_type != LOCAL_BOOL)) {
            snprintf(diagnostic->message, sizeof(diagnostic->message), "boolean operator requires Bool operands"); return 0;
        }
        *type = LOCAL_BOOL;
    } else if (opcode == DEUS_COALESCE) {
        *type = left_type == LOCAL_NULL ? right_type : left_type == right_type ? left_type : LOCAL_VALUE;
    } else {
        int ordering = opcode >= DEUS_LESS && opcode <= DEUS_GREATER_EQUAL;
        int arithmetic = opcode >= DEUS_ADD_I64 && opcode <= DEUS_MOD_I64;
        if ((ordering || arithmetic) && ((left_type != LOCAL_I64) ||
                                       (right_type != LOCAL_I64))) {
            snprintf(diagnostic->message, sizeof(diagnostic->message), arithmetic ? "arithmetic requires I64 operands" : "ordering comparison requires I64 operands"); return 0;
        }
        *type = arithmetic ? LOCAL_I64 : LOCAL_BOOL;
    }
    return add_code(out, opcode, 0u);
}

int deus_analyze_and_generate(const DeusAstProgram *ast, DeusProgram *out,
                              DeusDiagnostic *diagnostic) {
    LocalSymbol locals[DEUS_MAX_LOCALS] = {{0}}; uint32_t local_count = 0u;
    int genesis = 0, began = 0, halted = 0, executor_locked = 0;
    uint32_t depth = 0u, futures = 0u;
    LocalType stack_types[SEMANTIC_STACK_MAX] = {0};
    const DeusAstInstruction *current_instruction = NULL;
    memset(out, 0, sizeof(*out));
    for (uint32_t index = 0; index < ast->count; index++) {
        const DeusAstInstruction *instruction = &ast->instructions[index];
        uint8_t opcode = instruction->opcode; uint32_t operand = 0u;
        current_instruction = instruction;
        if (halted) { semantic_error(diagnostic, instruction, "instruction after halt"); goto failed; }
        if (instruction->statement_kind == DEUS_AST_BIND_LOCAL) {
            uint32_t slot = 0u; LocalType value_type = LOCAL_NULL;
            if (!began) { semantic_error(diagnostic, instruction, "bind requires genesis"); goto failed; }
            for (; slot < local_count; slot++)
                if (locals[slot].length == instruction->symbol_length &&
                    !memcmp(locals[slot].name, instruction->symbol, instruction->symbol_length)) break;
            if (slot < local_count) { semantic_error(diagnostic, instruction, "local is already bound"); goto failed; }
            if (local_count == DEUS_MAX_LOCALS) { semantic_error(diagnostic, instruction, "program exceeds 256 locals"); goto failed; }
            if (instruction->expression) {
                if (!compile_expression(instruction->expression, locals, local_count, out, &value_type, diagnostic)) goto failed;
            } else if (instruction->expression_kind == DEUS_AST_EXPRESSION_STRING) {
                value_type = LOCAL_STRING;
                if (!intern_string(out, instruction->operand.string, instruction->string_length, &operand) ||
                    !add_code(out, DEUS_CONST, operand)) goto memory_failed;
            } else if (instruction->expression_kind == DEUS_AST_EXPRESSION_I64) {
                value_type = LOCAL_I64;
                if (!add_code(out, DEUS_CONST_I64, 0u)) goto memory_failed;
                out->code[out->code_count - 1u].immediate = instruction->expression_integer;
            } else if (instruction->expression_kind == DEUS_AST_EXPRESSION_BOOL) {
                value_type = LOCAL_BOOL;
                if (!add_code(out, DEUS_CONST_BOOL, instruction->expression_boolean ? 1u : 0u)) goto memory_failed;
            } else if (instruction->expression_kind == DEUS_AST_EXPRESSION_NULL) {
                value_type = LOCAL_NULL;
                if (!add_code(out, DEUS_CONST_NULL, 0u)) goto memory_failed;
            } else if (instruction->expression_kind == DEUS_AST_EXPRESSION_LOCAL) {
                uint32_t source_slot = 0u;
                for (; source_slot < local_count; source_slot++)
                    if (locals[source_slot].length == instruction->expression_symbol_length &&
                        !memcmp(locals[source_slot].name, instruction->expression_symbol, instruction->expression_symbol_length)) break;
                if (source_slot == local_count) { semantic_error(diagnostic, instruction, "unknown local in expression"); goto failed; }
                value_type = locals[source_slot].type;
                if (!add_code(out, DEUS_LOAD, source_slot)) goto memory_failed;
            } else if (instruction->expression_kind == DEUS_AST_EXPRESSION_HUNT) {
                value_type = LOCAL_DOCUMENT; executor_locked = 1;
                int template_result = compile_hunt_template(instruction, locals, local_count, out, diagnostic);
                if (!template_result) goto failed;
                if (template_result < 0 &&
                    (!intern_string(out, instruction->operand.string, instruction->string_length, &operand) ||
                     !add_code(out, DEUS_HUNT, operand))) goto memory_failed;
            } else if (instruction->expression_kind == DEUS_AST_EXPRESSION_CALL) {
                uint32_t source_slot = 0u;
                for (; source_slot < local_count; source_slot++)
                    if (locals[source_slot].length == instruction->expression_symbol_length &&
                        !memcmp(locals[source_slot].name, instruction->expression_symbol,
                                instruction->expression_symbol_length)) break;
                if (source_slot == local_count) { semantic_error(diagnostic, instruction, "unknown adapter input local"); goto failed; }
                if (!type_is_serializable(locals[source_slot].type)) { semantic_error(diagnostic, instruction, "call input must be a serializable value"); goto failed; }
                if (!adapter_name_valid(instruction->operand.string, instruction->string_length) ) {
                    semantic_error(diagnostic, instruction, "adapter name must use lowercase letters, digits, '.' or '-'"); goto failed;
                }
                value_type = LOCAL_VALUE; executor_locked = 1;
                if (!intern_string(out, instruction->operand.string, instruction->string_length, &operand) ||
                    !add_code(out, DEUS_LOAD, source_slot) || !add_code(out, DEUS_HOST_CALL, operand)) goto memory_failed;
            } else if (instruction->expression_kind == DEUS_AST_EXPRESSION_REAP) {
                uint32_t source_slot = 0u;
                for (; source_slot < local_count; source_slot++)
                    if (locals[source_slot].length == instruction->expression_symbol_length &&
                        !memcmp(locals[source_slot].name, instruction->expression_symbol, instruction->expression_symbol_length)) break;
                if (source_slot == local_count) { semantic_error(diagnostic, instruction, "unknown document local"); goto failed; }
                if (locals[source_slot].type != LOCAL_DOCUMENT) { semantic_error(diagnostic, instruction, "reap requires a Document local"); goto failed; }
                value_type = LOCAL_STRING;
                if (!intern_string(out, instruction->operand.string, instruction->string_length, &operand) ||
                    !add_code(out, DEUS_LOAD, source_slot) || !add_code(out, DEUS_REAP, operand)) goto memory_failed;
            } else if (instruction->expression_kind == DEUS_AST_EXPRESSION_JSON) {
                uint32_t source_slot = 0u;
                for (; source_slot < local_count; source_slot++)
                    if (locals[source_slot].length == instruction->expression_symbol_length &&
                        !memcmp(locals[source_slot].name, instruction->expression_symbol, instruction->expression_symbol_length)) break;
                if (source_slot == local_count) { semantic_error(diagnostic, instruction, "unknown document local"); goto failed; }
                if (locals[source_slot].type != LOCAL_DOCUMENT) { semantic_error(diagnostic, instruction, "json requires a Document local"); goto failed; }
                value_type = LOCAL_SCALAR;
                if (!intern_string(out, instruction->operand.string, instruction->string_length, &operand) ||
                    !add_code(out, DEUS_LOAD, source_slot) || !add_code(out, DEUS_JSON_PATH, operand)) goto memory_failed;
            } else if (instruction->expression_kind == DEUS_AST_EXPRESSION_RECORD) {
                value_type = LOCAL_RECORD;
                if (!add_code(out, DEUS_CONST_RECORD, 0u)) goto memory_failed;
            } else if (instruction->expression_kind == DEUS_AST_EXPRESSION_LIST) {
                value_type = LOCAL_LIST;
                if (!add_code(out, DEUS_CONST_LIST, 0u)) goto memory_failed;
            } else if (instruction->expression_kind == DEUS_AST_EXPRESSION_GET ||
                       instruction->expression_kind == DEUS_AST_EXPRESSION_AT ||
                       instruction->expression_kind == DEUS_AST_EXPRESSION_GET_OPTIONAL ||
                       instruction->expression_kind == DEUS_AST_EXPRESSION_AT_OPTIONAL) {
                uint32_t source_slot = 0u;
                int is_get = instruction->expression_kind == DEUS_AST_EXPRESSION_GET ||
                             instruction->expression_kind == DEUS_AST_EXPRESSION_GET_OPTIONAL;
                int is_optional = instruction->expression_kind == DEUS_AST_EXPRESSION_GET_OPTIONAL ||
                                  instruction->expression_kind == DEUS_AST_EXPRESSION_AT_OPTIONAL;
                for (; source_slot < local_count; source_slot++)
                    if (locals[source_slot].length == instruction->expression_symbol_length &&
                        !memcmp(locals[source_slot].name, instruction->expression_symbol, instruction->expression_symbol_length)) break;
                if (source_slot == local_count) { semantic_error(diagnostic, instruction, "unknown structured value local"); goto failed; }
                if ((is_get && locals[source_slot].type != LOCAL_RECORD && locals[source_slot].type != LOCAL_VALUE) ||
                    (!is_get && locals[source_slot].type != LOCAL_LIST && locals[source_slot].type != LOCAL_VALUE)) {
                    semantic_error(diagnostic, instruction, is_get ? "get requires a Record local" : "at requires a List local"); goto failed;
                }
                value_type = LOCAL_VALUE;
                if (!add_code(out, DEUS_LOAD, source_slot)) goto memory_failed;
                if (is_get) {
                    if (!intern_string(out, instruction->operand.string, instruction->string_length, &operand) ||
                        !add_code(out, is_optional ? DEUS_RECORD_GET_OPTIONAL : DEUS_RECORD_GET, operand)) goto memory_failed;
                } else if (!add_code(out, is_optional ? DEUS_LIST_AT_OPTIONAL : DEUS_LIST_AT,
                                    instruction->operand.number)) goto memory_failed;
            } else { semantic_error(diagnostic, instruction, "unsupported bind expression"); goto failed; }
            if (!add_code(out, DEUS_BIND, local_count)) goto memory_failed;
            locals[local_count++] = (LocalSymbol){instruction->symbol, instruction->symbol_length, value_type};
            continue;
        }
        if (instruction->statement_kind == DEUS_AST_SET_FIELD || instruction->statement_kind == DEUS_AST_PUSH_ITEM) {
            uint32_t target_slot = 0u, value_slot = 0u;
            for (; target_slot < local_count; target_slot++)
                if (locals[target_slot].length == instruction->symbol_length &&
                    !memcmp(locals[target_slot].name, instruction->symbol, instruction->symbol_length)) break;
            for (; value_slot < local_count; value_slot++)
                if (locals[value_slot].length == instruction->expression_symbol_length &&
                    !memcmp(locals[value_slot].name, instruction->expression_symbol, instruction->expression_symbol_length)) break;
            if (target_slot == local_count || value_slot == local_count) { semantic_error(diagnostic, instruction, "unknown local in compound mutation"); goto failed; }
            if ((instruction->statement_kind == DEUS_AST_SET_FIELD && locals[target_slot].type != LOCAL_RECORD) ||
                (instruction->statement_kind == DEUS_AST_PUSH_ITEM && locals[target_slot].type != LOCAL_LIST)) {
                semantic_error(diagnostic, instruction, "compound mutation target has the wrong type"); goto failed;
            }
            if (!type_is_serializable(locals[value_slot].type)) { semantic_error(diagnostic, instruction, "compound values require serializable items"); goto failed; }
            if (!add_code(out, DEUS_LOAD, target_slot) || !add_code(out, DEUS_LOAD, value_slot)) goto memory_failed;
            if (instruction->statement_kind == DEUS_AST_SET_FIELD) {
                if (!intern_string(out, instruction->operand.string, instruction->string_length, &operand) ||
                    !add_code(out, DEUS_RECORD_SET, operand)) goto memory_failed;
            } else if (!add_code(out, DEUS_LIST_PUSH, 0u)) goto memory_failed;
            continue;
        }
        if (instruction->statement_kind == DEUS_AST_LOAD_LOCAL) {
            uint32_t slot = 0u;
            if (!began) { semantic_error(diagnostic, instruction, "load requires genesis"); goto failed; }
            for (; slot < local_count; slot++)
                if (locals[slot].length == instruction->symbol_length &&
                    !memcmp(locals[slot].name, instruction->symbol, instruction->symbol_length)) break;
            if (slot == local_count) { semantic_error(diagnostic, instruction, "unknown local"); goto failed; }
            if (depth == SEMANTIC_STACK_MAX) { semantic_error(diagnostic, instruction, "stack overflow"); goto failed; }
            if (!add_code(out, DEUS_LOAD, slot)) goto memory_failed;
            stack_types[depth++] = locals[slot].type; continue;
        }
        if (instruction->operand_kind == DEUS_AST_OPERAND_STRING) {
            if (!intern_string(out, instruction->operand.string, instruction->string_length, &operand)) goto memory_failed;
        } else if (instruction->operand_kind == DEUS_AST_OPERAND_U32) operand = instruction->operand.number;
        if (opcode == DEUS_GENESIS) {
            if (genesis++) { semantic_error(diagnostic, instruction, "genesis may appear only once"); goto failed; }
            began = 1;
        } else if (opcode == DEUS_LIMIT || opcode == DEUS_RETRY || opcode == DEUS_BACKOFF || opcode == DEUS_RATE) {
            if (executor_locked) { semantic_error(diagnostic, instruction, "executor configuration must precede network execution"); goto failed; }
            if (opcode == DEUS_LIMIT && (operand == 0u || operand > 256u)) { semantic_error(diagnostic, instruction, "limit must be between 1 and 256"); goto failed; }
            if (opcode == DEUS_RETRY && operand > 16u) { semantic_error(diagnostic, instruction, "retry must be at most 16"); goto failed; }
            if (opcode == DEUS_BACKOFF && operand > 60000u) { semantic_error(diagnostic, instruction, "backoff must be at most 60000 ms"); goto failed; }
            if (opcode == DEUS_RATE && operand > 10000u) { semantic_error(diagnostic, instruction, "rate must be at most 10000 rps"); goto failed; }
        } else if (opcode == DEUS_HUNT) {
            if (depth == SEMANTIC_STACK_MAX) { semantic_error(diagnostic, instruction, "stack overflow"); goto failed; }
            executor_locked = 1; stack_types[depth++] = LOCAL_DOCUMENT;
        } else if (opcode == DEUS_FORK) {
            if (depth == SEMANTIC_STACK_MAX) { semantic_error(diagnostic, instruction, "stack overflow"); goto failed; }
            executor_locked = 1; stack_types[depth++] = LOCAL_VALUE; futures++;
        }
        else if (opcode == DEUS_AWAIT) {
            if (!depth || !futures) { semantic_error(diagnostic, instruction, "await requires a future on stack"); goto failed; }
            futures--; stack_types[depth - 1u] = LOCAL_DOCUMENT;
        } else if (opcode == DEUS_JOIN) {
            if (operand == 0u || operand > futures || operand > depth) { semantic_error(diagnostic, instruction, "join exceeds pending futures"); goto failed; }
            for (uint32_t join_index = depth - operand; join_index < depth; join_index++) stack_types[join_index] = LOCAL_DOCUMENT;
            futures -= operand;
        } else if (opcode == DEUS_REAP) {
            if (!depth || futures || stack_types[depth - 1u] != LOCAL_DOCUMENT) { semantic_error(diagnostic, instruction, "reap requires a resolved Document stack"); goto failed; }
            stack_types[depth - 1u] = LOCAL_STRING;
        } else if (opcode == DEUS_EMIT || opcode == DEUS_DEBUG) {
            if (!depth || futures) { semantic_error(diagnostic, instruction, opcode == DEUS_EMIT ? "emit requires resolved extraction output" : "debug requires resolved extraction output"); goto failed; }
            if (!type_is_serializable(stack_types[depth - 1u])) { semantic_error(diagnostic, instruction, opcode == DEUS_EMIT ? "emit requires a serializable value, not Document" : "debug requires a serializable value, not Document"); goto failed; }
            depth--;
        } else if (opcode == DEUS_HALT) {
            if (futures) { semantic_error(diagnostic, instruction, "halt with unresolved futures"); goto failed; }
            halted = 1;
        }
        if (!add_code(out, opcode, operand)) goto memory_failed;
    }
    if (genesis != 1) {
        diagnostic->line = ast->count ? ast->instructions[ast->count - 1u].line : 1u;
        diagnostic->column = ast->count ? ast->instructions[ast->count - 1u].column : 1u;
        snprintf(diagnostic->message, sizeof(diagnostic->message), "program requires exactly one genesis"); goto failed;
    }
    if (!halted) {
        diagnostic->line = ast->count ? ast->instructions[ast->count - 1u].line : 1u;
        diagnostic->column = ast->count ? ast->instructions[ast->count - 1u].column : 1u;
        snprintf(diagnostic->message, sizeof(diagnostic->message), "program requires terminal halt"); goto failed;
    }
    return 1;
memory_failed:
    if (current_instruction) semantic_error(diagnostic, current_instruction, "out of memory");
    else { diagnostic->line = 1u; diagnostic->column = 1u; snprintf(diagnostic->message, sizeof(diagnostic->message), "out of memory"); }
failed:
    deus_program_free(out); return 0;
}
