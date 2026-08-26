#include "deus_compiler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { const char *name; uint8_t opcode; DeusAstOperandKind operand; } InstructionSpec;
static const InstructionSpec SPECS[] = {
    {"omni", DEUS_OMNI, DEUS_AST_OPERAND_STRING}, {"genesis", DEUS_GENESIS, DEUS_AST_OPERAND_NONE},
    {"hunt", DEUS_HUNT, DEUS_AST_OPERAND_STRING}, {"reap", DEUS_REAP, DEUS_AST_OPERAND_STRING},
    {"halt", DEUS_HALT, DEUS_AST_OPERAND_NONE}, {"emit", DEUS_EMIT, DEUS_AST_OPERAND_NONE},
    {"fork", DEUS_FORK, DEUS_AST_OPERAND_STRING}, {"await", DEUS_AWAIT, DEUS_AST_OPERAND_NONE},
    {"join", DEUS_JOIN, DEUS_AST_OPERAND_U32}, {"limit", DEUS_LIMIT, DEUS_AST_OPERAND_U32},
    {"retry", DEUS_RETRY, DEUS_AST_OPERAND_U32}, {"backoff", DEUS_BACKOFF, DEUS_AST_OPERAND_U32},
    {"rate", DEUS_RATE, DEUS_AST_OPERAND_U32}
};

static int same_word(const DeusToken *token, const char *word) {
    size_t length = strlen(word);
    return token->length == length && !memcmp(token->start, word, length);
}

static int pure_expression_start(const DeusToken *token) {
    return token->kind == DEUS_TOKEN_STRING || token->kind == DEUS_TOKEN_NUMBER ||
           token->kind == DEUS_TOKEN_LPAREN ||
           (token->kind == DEUS_TOKEN_IDENTIFIER &&
            !same_word(token, "hunt") && !same_word(token, "reap") && !same_word(token, "json") &&
            !same_word(token, "get") && !same_word(token, "get?") && !same_word(token, "at") &&
            !same_word(token, "at?") && !same_word(token, "record") && !same_word(token, "list"));
}

static unsigned distance(const DeusToken *token, const char *word) {
    size_t word_length = strlen(word); unsigned previous[65], current[65];
    if (token->length > 64u || word_length > 64u) return UINT32_MAX;
    for (size_t column = 0; column <= word_length; column++) previous[column] = (unsigned)column;
    for (size_t row = 1; row <= token->length; row++) {
        current[0] = (unsigned)row;
        for (size_t column = 1; column <= word_length; column++) {
            unsigned deletion = previous[column] + 1u;
            unsigned insertion = current[column - 1u] + 1u;
            unsigned substitution = previous[column - 1u] +
                (token->start[row - 1u] == word[column - 1u] ? 0u : 1u);
            unsigned best = deletion < insertion ? deletion : insertion;
            current[column] = best < substitution ? best : substitution;
        }
        memcpy(previous, current, (word_length + 1u) * sizeof(*previous));
    }
    return previous[word_length];
}

static const InstructionSpec *find_spec(const DeusToken *token, const char **suggestion) {
    const InstructionSpec *best = NULL; unsigned best_distance = UINT32_MAX;
    for (size_t index = 0; index < sizeof(SPECS) / sizeof(SPECS[0]); index++) {
        unsigned current;
        if (same_word(token, SPECS[index].name)) return &SPECS[index];
        current = distance(token, SPECS[index].name);
        if (current < best_distance) { best_distance = current; best = &SPECS[index]; }
    }
    *suggestion = best_distance <= 2u ? best->name : NULL;
    return NULL;
}

static int append_instruction(DeusAstProgram *program, const DeusAstInstruction *instruction) {
    DeusAstInstruction *next;
    if (program->count == UINT32_MAX) return 0;
    next = (DeusAstInstruction *)realloc(program->instructions,
        ((size_t)program->count + 1u) * sizeof(*next));
    if (!next) return 0;
    program->instructions = next; program->instructions[program->count++] = *instruction; return 1;
}

static int copy_symbol(const DeusToken *token, DeusAstInstruction *instruction,
                       DeusDiagnostic *diagnostic) {
    if (token->length > UINT32_MAX) {
        diagnostic->line = token->line; diagnostic->column = token->column;
        snprintf(diagnostic->message, sizeof(diagnostic->message), "symbol exceeds u32 length"); return 0;
    }
    instruction->symbol = (char *)malloc(token->length + 1u);
    if (!instruction->symbol) {
        diagnostic->line = token->line; diagnostic->column = token->column;
        snprintf(diagnostic->message, sizeof(diagnostic->message), "out of memory"); return 0;
    }
    memcpy(instruction->symbol, token->start, token->length);
    instruction->symbol[token->length] = '\0'; instruction->symbol_length = (uint32_t)token->length;
    return 1;
}

static int parse_structured_literal(DeusLexer *lexer, DeusAstProgram *program,
                                    DeusAstInstruction *bind, int is_record, uint32_t depth,
                                    uint32_t hidden_symbol_base,
                                    DeusDiagnostic *diagnostic);

static int hidden_symbol(DeusAstProgram *program, DeusAstInstruction *bind,
                         uint32_t hidden_symbol_base, unsigned line,
                         unsigned column, DeusDiagnostic *diagnostic) {
    uint32_t identifier; char name[48]; int written;
    if (program->count > UINT32_MAX - hidden_symbol_base) return 0;
    identifier = hidden_symbol_base + program->count;
    written = snprintf(name, sizeof(name), "$literal_%u", identifier);
    if (written < 0 || (size_t)written >= sizeof(name)) return 0;
    bind->symbol = (char *)malloc((size_t)written + 1u);
    if (!bind->symbol) return 0;
    memcpy(bind->symbol, name, (size_t)written + 1u); bind->symbol_length = (uint32_t)written;
    bind->statement_kind = DEUS_AST_BIND_LOCAL; bind->line = line; bind->column = column;
    diagnostic->line = line; diagnostic->column = column; return 1;
}

static int literal_value_symbol(DeusLexer *lexer, DeusAstProgram *program, DeusToken *value,
                                uint32_t depth, uint32_t hidden_symbol_base,
                                char **symbol, uint32_t *symbol_length,
                                DeusDiagnostic *diagnostic) {
    DeusAstInstruction generated = {0};
    if (value->length > UINT32_MAX) {
        diagnostic->line = value->line; diagnostic->column = value->column;
        snprintf(diagnostic->message, sizeof(diagnostic->message), "literal value exceeds u32 length"); return 0;
    }
    if (value->kind == DEUS_TOKEN_IDENTIFIER && !same_word(value, "true") &&
        !same_word(value, "false") && !same_word(value, "null")) {
        *symbol = (char *)malloc(value->length + 1u); if (!*symbol) return 0;
        memcpy(*symbol, value->start, value->length); (*symbol)[value->length] = '\0';
        *symbol_length = (uint32_t)value->length; return 1;
    }
    if (!hidden_symbol(program, &generated, hidden_symbol_base,
                       value->line, value->column, diagnostic)) return 0;
    if (value->kind == DEUS_TOKEN_STRING) {
        generated.expression_kind = DEUS_AST_EXPRESSION_STRING;
        generated.operand_kind = DEUS_AST_OPERAND_STRING; generated.operand.string = value->owned;
        generated.string_length = (uint32_t)value->length; value->owned = NULL;
    } else if (value->kind == DEUS_TOKEN_NUMBER) {
        generated.expression_kind = DEUS_AST_EXPRESSION_I64; generated.expression_integer = value->integer;
    } else if (value->kind == DEUS_TOKEN_IDENTIFIER && same_word(value, "true")) {
        generated.expression_kind = DEUS_AST_EXPRESSION_BOOL; generated.expression_boolean = 1;
    } else if (value->kind == DEUS_TOKEN_IDENTIFIER && same_word(value, "false")) {
        generated.expression_kind = DEUS_AST_EXPRESSION_BOOL;
    } else if (value->kind == DEUS_TOKEN_IDENTIFIER && same_word(value, "null")) {
        generated.expression_kind = DEUS_AST_EXPRESSION_NULL;
    } else if (value->kind == DEUS_TOKEN_LBRACE || value->kind == DEUS_TOKEN_LBRACKET) {
        int nested_record = value->kind == DEUS_TOKEN_LBRACE;
        if (depth >= 32u) {
            snprintf(diagnostic->message, sizeof(diagnostic->message), "structured literal exceeds 32 levels");
            free(generated.symbol); return 0;
        }
        if (!parse_structured_literal(lexer, program, &generated, nested_record,
                                      depth + 1u, hidden_symbol_base,
                                      diagnostic)) return 0;
        goto copy_generated_symbol;
    } else {
        diagnostic->line = value->line; diagnostic->column = value->column;
        snprintf(diagnostic->message, sizeof(diagnostic->message), "unsupported structured literal value");
        free(generated.symbol); return 0;
    }
    if (!append_instruction(program, &generated)) {
        free(generated.operand.string); free(generated.symbol); return 0;
    }
copy_generated_symbol:
    *symbol = (char *)malloc((size_t)generated.symbol_length + 1u); if (!*symbol) return 0;
    memcpy(*symbol, generated.symbol, (size_t)generated.symbol_length + 1u);
    *symbol_length = generated.symbol_length; return 1;
}

static int parse_structured_literal(DeusLexer *lexer, DeusAstProgram *program,
                                    DeusAstInstruction *bind, int is_record, uint32_t depth,
                                    uint32_t hidden_symbol_base,
                                    DeusDiagnostic *diagnostic) {
    DeusToken current = {0}; DeusTokenKind closing = is_record ? DEUS_TOKEN_RBRACE : DEUS_TOKEN_RBRACKET;
    bind->expression_kind = is_record ? DEUS_AST_EXPRESSION_RECORD : DEUS_AST_EXPRESSION_LIST;
    if (!append_instruction(program, bind)) {
        free(bind->symbol); bind->symbol = NULL;
        snprintf(diagnostic->message, sizeof(diagnostic->message), "out of memory"); return 0;
    }
    if (!deus_lexer_next(lexer, &current, diagnostic)) return 0;
    if (current.kind == closing) { deus_token_dispose(&current); return 1; }
    for (;;) {
        DeusAstInstruction mutation = {0}; DeusToken value = {0}, separator = {0};
        mutation.statement_kind = is_record ? DEUS_AST_SET_FIELD : DEUS_AST_PUSH_ITEM;
        mutation.line = current.line; mutation.column = current.column;
        mutation.symbol = (char *)malloc((size_t)bind->symbol_length + 1u);
        if (!mutation.symbol) goto failed;
        memcpy(mutation.symbol, bind->symbol, (size_t)bind->symbol_length + 1u);
        mutation.symbol_length = bind->symbol_length;
        if (is_record) {
            DeusToken colon = {0};
            if (current.kind != DEUS_TOKEN_STRING || current.length > UINT32_MAX) {
                diagnostic->line = current.line; diagnostic->column = current.column;
                snprintf(diagnostic->message, sizeof(diagnostic->message), "record literal requires a quoted field name"); goto failed;
            }
            mutation.operand_kind = DEUS_AST_OPERAND_STRING;
            mutation.operand.string = current.owned; mutation.string_length = (uint32_t)current.length; current.owned = NULL;
            if (!deus_lexer_next(lexer, &colon, diagnostic) || colon.kind != DEUS_TOKEN_COLON) {
                diagnostic->line = colon.line; diagnostic->column = colon.column;
                snprintf(diagnostic->message, sizeof(diagnostic->message), "record field requires ':'");
                deus_token_dispose(&colon); goto failed;
            }
            deus_token_dispose(&colon);
            if (!deus_lexer_next(lexer, &value, diagnostic)) goto failed;
        } else value = current, memset(&current, 0, sizeof(current));
        if (!literal_value_symbol(lexer, program, &value, depth,
                                  hidden_symbol_base,
                                  &mutation.expression_symbol, &mutation.expression_symbol_length,
                                  diagnostic)) goto failed;
        deus_token_dispose(&value); deus_token_dispose(&current);
        if (!append_instruction(program, &mutation)) goto failed;
        memset(&mutation, 0, sizeof(mutation));
        if (!deus_lexer_next(lexer, &separator, diagnostic)) return 0;
        if (separator.kind == closing) { deus_token_dispose(&separator); return 1; }
        if (separator.kind != DEUS_TOKEN_COMMA) {
            diagnostic->line = separator.line; diagnostic->column = separator.column;
            snprintf(diagnostic->message, sizeof(diagnostic->message), "structured literal requires ',' or closing delimiter");
            deus_token_dispose(&separator); return 0;
        }
        deus_token_dispose(&separator);
        if (!deus_lexer_next(lexer, &current, diagnostic)) return 0;
        if (current.kind == closing) {
            diagnostic->line = current.line; diagnostic->column = current.column;
            snprintf(diagnostic->message, sizeof(diagnostic->message), "trailing comma is not allowed in structured literal");
            deus_token_dispose(&current); return 0;
        }
        continue;
failed:
        deus_token_dispose(&current); deus_token_dispose(&value);
        free(mutation.operand.string); free(mutation.expression_symbol); free(mutation.symbol); return 0;
    }
}

int deus_parse_ast_fragment(const char *source, size_t length,
                            uint32_t hidden_symbol_base,
                            DeusAstProgram *out,
                            DeusDiagnostic *diagnostic) {
    DeusLexer lexer; DeusToken token;
    memset(out, 0, sizeof(*out)); deus_lexer_init(&lexer, source, length);
    for (;;) {
        const InstructionSpec *spec; const char *suggestion = NULL;
        DeusAstInstruction instruction = {0}; DeusToken operand;
        if (!deus_lexer_next(&lexer, &token, diagnostic)) goto failed;
        if (token.kind == DEUS_TOKEN_EOF) return 1;
        if (token.kind != DEUS_TOKEN_IDENTIFIER) {
            diagnostic->line = token.line; diagnostic->column = token.column;
            snprintf(diagnostic->message, sizeof(diagnostic->message), "expected instruction"); goto failed;
        }
        instruction.line = token.line; instruction.column = token.column;
        if (same_word(&token, "bind") || same_word(&token, "load") ||
            same_word(&token, "set") || same_word(&token, "push")) {
            DeusToken symbol = {0};
            instruction.statement_kind = same_word(&token, "bind") ? DEUS_AST_BIND_LOCAL :
                (same_word(&token, "load") ? DEUS_AST_LOAD_LOCAL :
                 (same_word(&token, "set") ? DEUS_AST_SET_FIELD : DEUS_AST_PUSH_ITEM));
            if (!deus_lexer_next(&lexer, &symbol, diagnostic)) goto failed;
            if (symbol.kind != DEUS_TOKEN_IDENTIFIER) {
                diagnostic->line = symbol.line; diagnostic->column = symbol.column;
                snprintf(diagnostic->message, sizeof(diagnostic->message), "expected local name");
                deus_token_dispose(&symbol); goto failed;
            }
            if (same_word(&symbol, "true") || same_word(&symbol, "false") || same_word(&symbol, "null")) {
                diagnostic->line = symbol.line; diagnostic->column = symbol.column;
                snprintf(diagnostic->message, sizeof(diagnostic->message), "literal name cannot be used as a local");
                deus_token_dispose(&symbol); goto failed;
            }
            if (symbol.length && symbol.start[symbol.length - 1u] == '?') {
                diagnostic->line = symbol.line; diagnostic->column = symbol.column;
                snprintf(diagnostic->message, sizeof(diagnostic->message), "local names cannot end with '?'");
                deus_token_dispose(&symbol); goto failed;
            }
            if (!copy_symbol(&symbol, &instruction, diagnostic)) { deus_token_dispose(&symbol); goto failed; }
            deus_token_dispose(&symbol);
            if (instruction.statement_kind == DEUS_AST_SET_FIELD || instruction.statement_kind == DEUS_AST_PUSH_ITEM) {
                DeusToken key = {0}, value = {0};
                if (instruction.statement_kind == DEUS_AST_SET_FIELD) {
                    if (!deus_lexer_next(&lexer, &key, diagnostic) || key.kind != DEUS_TOKEN_STRING || key.length > UINT32_MAX) {
                        diagnostic->line = key.line; diagnostic->column = key.column;
                        snprintf(diagnostic->message, sizeof(diagnostic->message), "set requires a quoted field name");
                        deus_token_dispose(&key); free(instruction.symbol); goto failed;
                    }
                    instruction.operand_kind = DEUS_AST_OPERAND_STRING; instruction.operand.string = key.owned;
                    instruction.string_length = (uint32_t)key.length; key.owned = NULL; deus_token_dispose(&key);
                }
                if (!deus_lexer_next(&lexer, &value, diagnostic) || value.kind != DEUS_TOKEN_IDENTIFIER || value.length > UINT32_MAX) {
                    diagnostic->line = value.line; diagnostic->column = value.column;
                    snprintf(diagnostic->message, sizeof(diagnostic->message), "%s requires a value local",
                             instruction.statement_kind == DEUS_AST_SET_FIELD ? "set" : "push");
                    deus_token_dispose(&value); free(instruction.operand.string); free(instruction.symbol); goto failed;
                }
                instruction.expression_symbol = (char *)malloc(value.length + 1u);
                if (!instruction.expression_symbol) { deus_token_dispose(&value); free(instruction.operand.string); free(instruction.symbol); goto failed; }
                memcpy(instruction.expression_symbol, value.start, value.length); instruction.expression_symbol[value.length] = '\0';
                instruction.expression_symbol_length = (uint32_t)value.length; deus_token_dispose(&value);
                if (!append_instruction(out, &instruction)) {
                    free(instruction.expression_symbol); free(instruction.operand.string); free(instruction.symbol); goto failed;
                }
                continue;
            }
            if (instruction.statement_kind == DEUS_AST_BIND_LOCAL) {
                DeusToken equal = {0};
                if (!deus_lexer_next(&lexer, &equal, diagnostic)) { free(instruction.symbol); goto failed; }
                if (equal.kind != DEUS_TOKEN_EQUAL) {
                    diagnostic->line = equal.line; diagnostic->column = equal.column;
                    snprintf(diagnostic->message, sizeof(diagnostic->message), "expected '=' after local name");
                    deus_token_dispose(&equal); free(instruction.symbol); goto failed;
                }
                deus_token_dispose(&equal);
                {
                    const char *expression_start = lexer.cursor, *expression_end = expression_start;
                    DeusLexer probe; DeusToken first = {0};
                    while (expression_end < lexer.end && *expression_end != '\n' && *expression_end != '\r') expression_end++;
                    deus_lexer_init(&probe, expression_start, (size_t)(expression_end - expression_start));
                    if (!deus_lexer_next(&probe, &first, diagnostic)) { free(instruction.symbol); goto failed; }
                    if (pure_expression_start(&first)) {
                        if (!deus_parse_expression(expression_start, (size_t)(expression_end - expression_start),
                                                   &instruction.expression, diagnostic)) {
                            deus_token_dispose(&first); free(instruction.symbol); goto failed;
                        }
                        lexer.column += (unsigned)(expression_end - lexer.cursor); lexer.cursor = expression_end;
                    }
                    deus_token_dispose(&first);
                }
                if (!instruction.expression) {
                    if (!deus_lexer_next(&lexer, &operand, diagnostic)) { free(instruction.symbol); goto failed; }
                if (operand.kind == DEUS_TOKEN_LBRACE || operand.kind == DEUS_TOKEN_LBRACKET) {
                    int is_record = operand.kind == DEUS_TOKEN_LBRACE;
                    deus_token_dispose(&operand);
                    if (!parse_structured_literal(&lexer, out, &instruction,
                                                  is_record, 1u,
                                                  hidden_symbol_base,
                                                  diagnostic)) goto failed;
                    continue;
                } else if (operand.kind == DEUS_TOKEN_STRING && operand.length <= UINT32_MAX) {
                    instruction.expression_kind = DEUS_AST_EXPRESSION_STRING;
                    instruction.operand_kind = DEUS_AST_OPERAND_STRING;
                    instruction.operand.string = operand.owned; instruction.string_length = (uint32_t)operand.length;
                    operand.owned = NULL;
                } else if (operand.kind == DEUS_TOKEN_NUMBER) {
                    instruction.expression_kind = DEUS_AST_EXPRESSION_I64;
                    instruction.expression_integer = operand.integer;
                } else if (operand.kind == DEUS_TOKEN_IDENTIFIER && same_word(&operand, "true")) {
                    instruction.expression_kind = DEUS_AST_EXPRESSION_BOOL; instruction.expression_boolean = 1;
                } else if (operand.kind == DEUS_TOKEN_IDENTIFIER && same_word(&operand, "false")) {
                    instruction.expression_kind = DEUS_AST_EXPRESSION_BOOL;
                } else if (operand.kind == DEUS_TOKEN_IDENTIFIER && same_word(&operand, "null")) {
                    instruction.expression_kind = DEUS_AST_EXPRESSION_NULL;
                } else if (operand.kind == DEUS_TOKEN_IDENTIFIER && same_word(&operand, "record")) {
                    instruction.expression_kind = DEUS_AST_EXPRESSION_RECORD;
                } else if (operand.kind == DEUS_TOKEN_IDENTIFIER && same_word(&operand, "list")) {
                    instruction.expression_kind = DEUS_AST_EXPRESSION_LIST;
                } else if (operand.kind == DEUS_TOKEN_IDENTIFIER && same_word(&operand, "hunt")) {
                    DeusToken url = {0};
                    if (!deus_lexer_next(&lexer, &url, diagnostic)) {
                        deus_token_dispose(&operand); free(instruction.symbol); goto failed;
                    }
                    if (url.kind != DEUS_TOKEN_STRING || url.length > UINT32_MAX) {
                        diagnostic->line = url.line; diagnostic->column = url.column;
                        snprintf(diagnostic->message, sizeof(diagnostic->message), "hunt expression requires a quoted URL");
                        deus_token_dispose(&url); deus_token_dispose(&operand); free(instruction.symbol); goto failed;
                    }
                    instruction.expression_kind = DEUS_AST_EXPRESSION_HUNT;
                    instruction.operand_kind = DEUS_AST_OPERAND_STRING;
                    instruction.operand.string = url.owned; instruction.string_length = (uint32_t)url.length;
                    url.owned = NULL; deus_token_dispose(&url);
                } else if (operand.kind == DEUS_TOKEN_IDENTIFIER &&
                           (same_word(&operand, "reap") || same_word(&operand, "json"))) {
                    int is_json = same_word(&operand, "json");
                    DeusToken document = {0}, selector = {0};
                    if (!deus_lexer_next(&lexer, &document, diagnostic)) {
                        deus_token_dispose(&operand); free(instruction.symbol); goto failed;
                    }
                    if (document.kind != DEUS_TOKEN_IDENTIFIER || document.length > UINT32_MAX) {
                        diagnostic->line = document.line; diagnostic->column = document.column;
                        snprintf(diagnostic->message, sizeof(diagnostic->message), "%s expression requires a document local", is_json ? "json" : "reap");
                        deus_token_dispose(&document); deus_token_dispose(&operand); free(instruction.symbol); goto failed;
                    }
                    instruction.expression_symbol = (char *)malloc(document.length + 1u);
                    if (!instruction.expression_symbol) {
                        diagnostic->line = document.line; diagnostic->column = document.column;
                        snprintf(diagnostic->message, sizeof(diagnostic->message), "out of memory");
                        deus_token_dispose(&document); deus_token_dispose(&operand); free(instruction.symbol); goto failed;
                    }
                    memcpy(instruction.expression_symbol, document.start, document.length);
                    instruction.expression_symbol[document.length] = '\0';
                    instruction.expression_symbol_length = (uint32_t)document.length;
                    deus_token_dispose(&document);
                    if (!deus_lexer_next(&lexer, &selector, diagnostic)) {
                        deus_token_dispose(&operand); free(instruction.expression_symbol); free(instruction.symbol); goto failed;
                    }
                    if (selector.kind != DEUS_TOKEN_STRING || selector.length > UINT32_MAX) {
                        diagnostic->line = selector.line; diagnostic->column = selector.column;
                        snprintf(diagnostic->message, sizeof(diagnostic->message), "%s expression requires a quoted %s", is_json ? "json" : "reap", is_json ? "path" : "selector");
                        deus_token_dispose(&selector); deus_token_dispose(&operand);
                        free(instruction.expression_symbol); free(instruction.symbol); goto failed;
                    }
                    instruction.expression_kind = is_json ? DEUS_AST_EXPRESSION_JSON : DEUS_AST_EXPRESSION_REAP;
                    instruction.operand_kind = DEUS_AST_OPERAND_STRING;
                    instruction.operand.string = selector.owned; instruction.string_length = (uint32_t)selector.length;
                    selector.owned = NULL; deus_token_dispose(&selector);
                } else if (operand.kind == DEUS_TOKEN_IDENTIFIER &&
                           (same_word(&operand, "get") || same_word(&operand, "at") ||
                            same_word(&operand, "get?") || same_word(&operand, "at?"))) {
                    int is_get = same_word(&operand, "get") || same_word(&operand, "get?");
                    int is_optional = same_word(&operand, "get?") || same_word(&operand, "at?");
                    const char *operation = is_get ? (is_optional ? "get?" : "get") : (is_optional ? "at?" : "at");
                    DeusToken container_token = {0}, accessor = {0};
                    if (!deus_lexer_next(&lexer, &container_token, diagnostic) ||
                        container_token.kind != DEUS_TOKEN_IDENTIFIER || container_token.length > UINT32_MAX) {
                        diagnostic->line = container_token.line; diagnostic->column = container_token.column;
                        snprintf(diagnostic->message, sizeof(diagnostic->message), "%s requires a source local", operation);
                        deus_token_dispose(&container_token); deus_token_dispose(&operand); free(instruction.symbol); goto failed;
                    }
                    instruction.expression_symbol = (char *)malloc(container_token.length + 1u);
                    if (!instruction.expression_symbol) {
                        deus_token_dispose(&container_token); deus_token_dispose(&operand); free(instruction.symbol); goto failed;
                    }
                    memcpy(instruction.expression_symbol, container_token.start, container_token.length);
                    instruction.expression_symbol[container_token.length] = '\0';
                    instruction.expression_symbol_length = (uint32_t)container_token.length;
                    deus_token_dispose(&container_token);
                    if (!deus_lexer_next(&lexer, &accessor, diagnostic) ||
                        (is_get ? accessor.kind != DEUS_TOKEN_STRING :
                         (accessor.kind != DEUS_TOKEN_NUMBER || accessor.integer < 0 || (uint64_t)accessor.integer > UINT32_MAX))) {
                        diagnostic->line = accessor.line; diagnostic->column = accessor.column;
                        snprintf(diagnostic->message, sizeof(diagnostic->message), "%s requires %s", operation,
                                 is_get ? "a quoted field name" : "an unsigned list index");
                        deus_token_dispose(&accessor); deus_token_dispose(&operand);
                        free(instruction.expression_symbol); free(instruction.symbol); goto failed;
                    }
                    instruction.expression_kind = is_get ?
                        (is_optional ? DEUS_AST_EXPRESSION_GET_OPTIONAL : DEUS_AST_EXPRESSION_GET) :
                        (is_optional ? DEUS_AST_EXPRESSION_AT_OPTIONAL : DEUS_AST_EXPRESSION_AT);
                    if (is_get) {
                        instruction.operand_kind = DEUS_AST_OPERAND_STRING;
                        instruction.operand.string = accessor.owned;
                        instruction.string_length = (uint32_t)accessor.length; accessor.owned = NULL;
                    } else {
                        instruction.operand_kind = DEUS_AST_OPERAND_U32;
                        instruction.operand.number = accessor.number;
                    }
                    deus_token_dispose(&accessor);
                } else if (operand.kind == DEUS_TOKEN_IDENTIFIER && operand.length <= UINT32_MAX) {
                    instruction.expression_symbol = (char *)malloc(operand.length + 1u);
                    if (!instruction.expression_symbol) {
                        diagnostic->line = operand.line; diagnostic->column = operand.column;
                        snprintf(diagnostic->message, sizeof(diagnostic->message), "out of memory");
                        deus_token_dispose(&operand); free(instruction.symbol); goto failed;
                    }
                    memcpy(instruction.expression_symbol, operand.start, operand.length);
                    instruction.expression_symbol[operand.length] = '\0';
                    instruction.expression_symbol_length = (uint32_t)operand.length;
                    instruction.expression_kind = DEUS_AST_EXPRESSION_LOCAL;
                } else {
                    diagnostic->line = operand.line; diagnostic->column = operand.column;
                    snprintf(diagnostic->message, sizeof(diagnostic->message), "expected bind expression");
                    deus_token_dispose(&operand); free(instruction.symbol); goto failed;
                }
                deus_token_dispose(&operand);
                }
            }
            if (!append_instruction(out, &instruction)) {
                free(instruction.symbol);
                if (instruction.operand_kind == DEUS_AST_OPERAND_STRING) free(instruction.operand.string);
                free(instruction.expression_symbol);
                diagnostic->line = instruction.line; diagnostic->column = instruction.column;
                snprintf(diagnostic->message, sizeof(diagnostic->message), "out of memory"); goto failed;
            }
            continue;
        }
        spec = find_spec(&token, &suggestion);
        if (!spec) {
            int shown = (int)(token.length > 64u ? 64u : token.length);
            diagnostic->line = token.line; diagnostic->column = token.column;
            if (suggestion) snprintf(diagnostic->message, sizeof(diagnostic->message),
                "unknown instruction '%.*s'; did you mean '%s'?", shown, token.start, suggestion);
            else snprintf(diagnostic->message, sizeof(diagnostic->message),
                "unknown instruction '%.*s'", shown, token.start);
            goto failed;
        }
        instruction.statement_kind = DEUS_AST_INSTRUCTION;
        instruction.opcode = spec->opcode; instruction.operand_kind = spec->operand;
        if (spec->operand != DEUS_AST_OPERAND_NONE) {
            if (!deus_lexer_next(&lexer, &operand, diagnostic)) goto failed;
            if (spec->operand == DEUS_AST_OPERAND_STRING && operand.kind == DEUS_TOKEN_STRING) {
                if (operand.length > UINT32_MAX) {
                    diagnostic->line = operand.line; diagnostic->column = operand.column;
                    snprintf(diagnostic->message, sizeof(diagnostic->message), "string exceeds u32 length");
                    deus_token_dispose(&operand); goto failed;
                }
                instruction.operand.string = operand.owned; instruction.string_length = (uint32_t)operand.length; operand.owned = NULL;
            } else if (spec->operand == DEUS_AST_OPERAND_U32 && operand.kind == DEUS_TOKEN_NUMBER &&
                       operand.integer >= 0 && (uint64_t)operand.integer <= UINT32_MAX)
                instruction.operand.number = operand.number;
            else {
                diagnostic->line = operand.line; diagnostic->column = operand.column;
                snprintf(diagnostic->message, sizeof(diagnostic->message), spec->operand == DEUS_AST_OPERAND_STRING ?
                    "expected quoted string" : "expected unsigned integer");
                deus_token_dispose(&operand); goto failed;
            }
            deus_token_dispose(&operand);
        }
        if (!append_instruction(out, &instruction)) {
            if (instruction.operand_kind == DEUS_AST_OPERAND_STRING) free(instruction.operand.string);
            free(instruction.symbol);
            diagnostic->line = instruction.line; diagnostic->column = instruction.column;
            snprintf(diagnostic->message, sizeof(diagnostic->message), "out of memory"); goto failed;
        }
    }
failed:
    deus_token_dispose(&token); deus_ast_free(out); return 0;
}

int deus_parse_ast(const char *source, size_t length, DeusAstProgram *out,
                   DeusDiagnostic *diagnostic) {
    return deus_parse_ast_fragment(source, length, 0u, out, diagnostic);
}
