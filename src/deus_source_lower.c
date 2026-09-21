#include "deus_source_lower.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail(DeusDiagnostic *diagnostic, unsigned line, unsigned column,
                const char *message) {
    diagnostic->line = line;
    diagnostic->column = column;
    snprintf(diagnostic->message, sizeof(diagnostic->message), "%s", message);
    return 0;
}

static int append_instruction(DeusAstProgram *program,
                              const DeusAstInstruction *instruction,
                              DeusDiagnostic *diagnostic) {
    DeusAstInstruction *next;
    if (program->count == UINT32_MAX)
        return fail(diagnostic, instruction->line, instruction->column,
                    "program has too many instructions");
    next = (DeusAstInstruction *)realloc(
        program->instructions,
        ((size_t)program->count + 1u) * sizeof(*next));
    if (!next)
        return fail(diagnostic, instruction->line, instruction->column,
                    "out of memory");
    program->instructions = next;
    program->instructions[program->count++] = *instruction;
    return 1;
}

static void shift_expression_lines(DeusExpressionNode *expression,
                                   unsigned line_offset) {
    if (!expression) return;
    expression->line += line_offset;
    shift_expression_lines(expression->left, line_offset);
    shift_expression_lines(expression->right, line_offset);
}

static int legacy_setting_kind(const DeusAstInstruction *instruction) {
    if (instruction->statement_kind != DEUS_AST_INSTRUCTION) return -1;
    switch (instruction->opcode) {
        case DEUS_LIMIT: return DEUS_SOURCE_LIMIT_WORKERS;
        case DEUS_RETRY: return DEUS_SOURCE_LIMIT_RETRY;
        case DEUS_BACKOFF: return DEUS_SOURCE_LIMIT_BACKOFF;
        case DEUS_RATE: return DEUS_SOURCE_LIMIT_RATE;
        default: return -1;
    }
}

static const char *setting_name(int kind) {
    static const char *const names[] = {"workers", "retry", "backoff", "rate"};
    return kind >= DEUS_SOURCE_LIMIT_WORKERS &&
           kind <= DEUS_SOURCE_LIMIT_RATE ? names[kind] : "setting";
}

static int reject_mixed(DeusDiagnostic *diagnostic, unsigned line,
                        unsigned column, int kind) {
    char message[192];
    snprintf(message, sizeof(message),
             "`%s` cannot appear both in `limits` and as a legacy setting",
             setting_name(kind));
    return fail(diagnostic, line, column, message);
}

static int append_raw(const DeusSourceAst *source, DeusSourceSpan span,
                      DeusAstProgram *out, DeusDiagnostic *diagnostic,
                      int modern_seen, unsigned *legacy_seen) {
    DeusAstProgram parsed;
    DeusDiagnostic local = {0};
    size_t index;
    unsigned line_offset = span.start.line - 1u;
    memset(&parsed, 0, sizeof(parsed));
    if (!deus_parse_ast_fragment(source->source + span.start.offset,
                                 span.end.offset - span.start.offset,
                                 out->count, &parsed, &local)) {
        if (local.line) local.line += line_offset;
        *diagnostic = local;
        return 0;
    }
    for (index = 0u; index < parsed.count; index++) {
        DeusAstInstruction *instruction = &parsed.instructions[index];
        int kind;
        instruction->line += line_offset;
        shift_expression_lines(instruction->expression, line_offset);
        if (instruction->statement_kind == DEUS_AST_INSTRUCTION &&
            (instruction->opcode == DEUS_GENESIS || instruction->opcode == DEUS_HALT)) {
            fail(diagnostic, instruction->line, instruction->column,
                 "explicit `genesis` and `halt` are not allowed inside a flow");
            deus_ast_free(&parsed);
            return 0;
        }
        kind = legacy_setting_kind(instruction);
        if (kind >= 0) {
            if (modern_seen) {
                reject_mixed(diagnostic, instruction->line, instruction->column, kind);
                deus_ast_free(&parsed);
                return 0;
            }
            *legacy_seen |= 1u << (unsigned)kind;
        }
    }
    for (index = 0u; index < parsed.count; index++) {
        if (!append_instruction(out, &parsed.instructions[index], diagnostic)) {
            size_t moved;
            for (moved = 0u; moved < index; moved++)
                memset(&parsed.instructions[moved], 0, sizeof(parsed.instructions[moved]));
            deus_ast_free(&parsed);
            return 0;
        }
        memset(&parsed.instructions[index], 0, sizeof(parsed.instructions[index]));
    }
    free(parsed.instructions);
    return 1;
}

static int append_limits(const DeusSourceLimitsBlock *block,
                         DeusAstProgram *out, DeusDiagnostic *diagnostic,
                         unsigned legacy_seen) {
    static const uint8_t opcodes[] = {
        DEUS_LIMIT, DEUS_RETRY, DEUS_BACKOFF, DEUS_RATE
    };
    unsigned kind;
    if (legacy_seen) {
        for (kind = DEUS_SOURCE_LIMIT_WORKERS;
             kind <= DEUS_SOURCE_LIMIT_RATE; kind++) {
            if (legacy_seen & (1u << kind))
                return reject_mixed(diagnostic, block->span.start.line,
                                    block->span.start.column, (int)kind);
        }
    }
    for (kind = DEUS_SOURCE_LIMIT_WORKERS; kind <= DEUS_SOURCE_LIMIT_RATE; kind++) {
        size_t index;
        for (index = 0u; index < block->entry_count; index++) {
            const DeusSourceLimitEntry *entry = &block->entries[index];
            DeusAstInstruction instruction;
            if ((unsigned)entry->kind != kind) continue;
            memset(&instruction, 0, sizeof(instruction));
            instruction.statement_kind = DEUS_AST_INSTRUCTION;
            instruction.opcode = opcodes[kind];
            instruction.operand_kind = DEUS_AST_OPERAND_U32;
            instruction.operand.number = entry->value;
            instruction.line = entry->value_span.start.line;
            instruction.column = entry->value_span.start.column;
            if (!append_instruction(out, &instruction, diagnostic)) return 0;
            break;
        }
    }
    return 1;
}

int deus_source_lower(const DeusSourceAst *source, DeusAstProgram *out,
                      DeusDiagnostic *diagnostic) {
    DeusAstInstruction boundary;
    unsigned legacy_seen = 0u;
    int modern_seen = 0;
    size_t index;
    if (!source || !out || !diagnostic) return 0;
    memset(out, 0, sizeof(*out));
    memset(&boundary, 0, sizeof(boundary));
    boundary.statement_kind = DEUS_AST_INSTRUCTION;
    boundary.opcode = DEUS_GENESIS;
    boundary.line = source->flow.span.start.line;
    boundary.column = source->flow.span.start.column;
    if (!append_instruction(out, &boundary, diagnostic)) return 0;
    for (index = 0u; index < source->flow.item_count; index++) {
        const DeusSourceFlowItem *item = &source->flow.items[index];
        if (item->kind == DEUS_SOURCE_FLOW_LIMITS) {
            if (!append_limits(&item->as.limits, out, diagnostic, legacy_seen))
                goto failed;
            modern_seen = 1;
        } else if (!append_raw(source, item->as.raw, out, diagnostic,
                               modern_seen, &legacy_seen)) goto failed;
    }
    memset(&boundary, 0, sizeof(boundary));
    boundary.statement_kind = DEUS_AST_INSTRUCTION;
    boundary.opcode = DEUS_HALT;
    boundary.line = source->flow.span.end.line;
    boundary.column = source->flow.span.end.column;
    if (!append_instruction(out, &boundary, diagnostic)) goto failed;
    return 1;
failed:
    deus_ast_free(out);
    return 0;
}
