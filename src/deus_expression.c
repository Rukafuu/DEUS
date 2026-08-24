#include "deus_compiler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { DeusLexer lexer; DeusToken token; DeusDiagnostic *diagnostic; uint32_t depth; } ExpressionParser;

static int word(const DeusToken *token, const char *value) {
    size_t length = strlen(value); return token->kind == DEUS_TOKEN_IDENTIFIER &&
        token->length == length && !memcmp(token->start, value, length);
}

static int next(ExpressionParser *parser) {
    deus_token_dispose(&parser->token);
    return deus_lexer_next(&parser->lexer, &parser->token, parser->diagnostic);
}

static DeusExpressionNode *node(ExpressionParser *parser, DeusExpressionNodeKind kind) {
    DeusExpressionNode *result = (DeusExpressionNode *)calloc(1, sizeof(*result));
    if (!result) return NULL;
    result->kind = kind; result->line = parser->token.line; result->column = parser->token.column; return result;
}

static DeusExpressionNode *parse_coalesce(ExpressionParser *parser);

static DeusExpressionNode *primary(ExpressionParser *parser) {
    DeusExpressionNode *result;
    if (++parser->depth > 64u) { snprintf(parser->diagnostic->message, sizeof(parser->diagnostic->message), "expression exceeds 64 levels"); return NULL; }
    if (parser->token.kind == DEUS_TOKEN_LPAREN) {
        if (!next(parser)) return NULL;
        result = parse_coalesce(parser);
        if (!result || parser->token.kind != DEUS_TOKEN_RPAREN) { deus_expression_free(result); snprintf(parser->diagnostic->message, sizeof(parser->diagnostic->message), "expected ')'"); return NULL; }
        if (!next(parser)) { deus_expression_free(result); return NULL; }
    } else if (word(&parser->token, "text") || word(&parser->token, "i64") || word(&parser->token, "bool")) {
        DeusExpressionOperator operation = word(&parser->token, "text") ? DEUS_EXPRESSION_OP_TEXT :
            word(&parser->token, "i64") ? DEUS_EXPRESSION_OP_I64 : DEUS_EXPRESSION_OP_BOOL;
        result = node(parser, DEUS_EXPRESSION_CONVERSION); if (!result || !next(parser)) return result;
        result->operator_kind = operation;
        if (parser->token.kind != DEUS_TOKEN_LPAREN || !next(parser)) { deus_expression_free(result); return NULL; }
        result->left = parse_coalesce(parser);
        if (!result->left || parser->token.kind != DEUS_TOKEN_RPAREN) { deus_expression_free(result); snprintf(parser->diagnostic->message, sizeof(parser->diagnostic->message), "conversion requires ')'"); return NULL; }
        if (!next(parser)) { deus_expression_free(result); return NULL; }
    } else if (parser->token.kind == DEUS_TOKEN_STRING || parser->token.kind == DEUS_TOKEN_NUMBER ||
               word(&parser->token, "true") || word(&parser->token, "false") || word(&parser->token, "null")) {
        result = node(parser, DEUS_EXPRESSION_LITERAL); if (!result) return NULL;
        if (parser->token.kind == DEUS_TOKEN_STRING) {
            result->literal_kind = DEUS_AST_EXPRESSION_STRING; result->literal.string = parser->token.owned;
            result->string_length = (uint32_t)parser->token.length; parser->token.owned = NULL;
        } else if (parser->token.kind == DEUS_TOKEN_NUMBER) {
            result->literal_kind = DEUS_AST_EXPRESSION_I64; result->literal.integer = parser->token.integer;
        } else if (word(&parser->token, "null")) result->literal_kind = DEUS_AST_EXPRESSION_NULL;
        else { result->literal_kind = DEUS_AST_EXPRESSION_BOOL; result->literal.boolean = word(&parser->token, "true"); }
        if (!next(parser)) { deus_expression_free(result); return NULL; }
    } else if (parser->token.kind == DEUS_TOKEN_IDENTIFIER) {
        result = node(parser, DEUS_EXPRESSION_LOCAL); if (!result) return NULL;
        result->symbol = (char *)malloc(parser->token.length + 1u);
        if (!result->symbol) { deus_expression_free(result); return NULL; }
        memcpy(result->symbol, parser->token.start, parser->token.length); result->symbol[parser->token.length] = '\0';
        result->symbol_length = (uint32_t)parser->token.length;
        if (!next(parser)) { deus_expression_free(result); return NULL; }
    } else { snprintf(parser->diagnostic->message, sizeof(parser->diagnostic->message), "expected expression"); return NULL; }
    parser->depth--; return result;
}

static DeusExpressionNode *unary(ExpressionParser *parser) {
    if (word(&parser->token, "not")) {
        DeusExpressionNode *result = node(parser, DEUS_EXPRESSION_UNARY); if (!result || !next(parser)) return result;
        result->operator_kind = DEUS_EXPRESSION_OP_NOT; result->left = unary(parser);
        if (!result->left) { deus_expression_free(result); return NULL; } return result;
    }
    return primary(parser);
}

static DeusExpressionNode *combine(ExpressionParser *parser, DeusExpressionNode *left,
                                   DeusExpressionOperator operation,
                                   DeusExpressionNode *(*right_parser)(ExpressionParser *)) {
    DeusExpressionNode *result = node(parser, DEUS_EXPRESSION_BINARY); if (!result || !next(parser)) { deus_expression_free(left); return result; }
    result->operator_kind = operation; result->left = left; result->right = right_parser(parser);
    if (!result->right) { deus_expression_free(result); return NULL; } return result;
}

static DeusExpressionNode *comparison(ExpressionParser *parser) {
    DeusExpressionNode *left = unary(parser); if (!left) return NULL;
    while (parser->token.kind >= DEUS_TOKEN_LESS && parser->token.kind <= DEUS_TOKEN_GREATER_EQUAL) {
        DeusExpressionOperator op = parser->token.kind == DEUS_TOKEN_LESS ? DEUS_EXPRESSION_OP_LESS :
            parser->token.kind == DEUS_TOKEN_LESS_EQUAL ? DEUS_EXPRESSION_OP_LESS_EQUAL :
            parser->token.kind == DEUS_TOKEN_GREATER ? DEUS_EXPRESSION_OP_GREATER : DEUS_EXPRESSION_OP_GREATER_EQUAL;
        left = combine(parser, left, op, unary); if (!left) return NULL;
    }
    return left;
}

static DeusExpressionNode *equality(ExpressionParser *parser) {
    DeusExpressionNode *left = comparison(parser); if (!left) return NULL;
    while (parser->token.kind == DEUS_TOKEN_EQUAL_EQUAL || parser->token.kind == DEUS_TOKEN_NOT_EQUAL) {
        DeusExpressionOperator op = parser->token.kind == DEUS_TOKEN_EQUAL_EQUAL ? DEUS_EXPRESSION_OP_EQUAL : DEUS_EXPRESSION_OP_NOT_EQUAL;
        left = combine(parser, left, op, comparison); if (!left) return NULL;
    }
    return left;
}

static DeusExpressionNode *logical_and(ExpressionParser *parser) {
    DeusExpressionNode *left = equality(parser); if (!left) return NULL;
    while (word(&parser->token, "and")) { left = combine(parser, left, DEUS_EXPRESSION_OP_AND, equality); if (!left) return NULL; }
    return left;
}

static DeusExpressionNode *logical_or(ExpressionParser *parser) {
    DeusExpressionNode *left = logical_and(parser); if (!left) return NULL;
    while (word(&parser->token, "or")) { left = combine(parser, left, DEUS_EXPRESSION_OP_OR, logical_and); if (!left) return NULL; }
    return left;
}

static DeusExpressionNode *parse_coalesce(ExpressionParser *parser) {
    DeusExpressionNode *left = logical_or(parser); if (!left) return NULL;
    if (parser->token.kind == DEUS_TOKEN_COALESCE) return combine(parser, left, DEUS_EXPRESSION_OP_COALESCE, parse_coalesce);
    return left;
}

int deus_parse_expression(const char *source, size_t length, DeusExpressionNode **out,
                          DeusDiagnostic *diagnostic) {
    ExpressionParser parser = {0}; *out = NULL; deus_lexer_init(&parser.lexer, source, length); parser.diagnostic = diagnostic;
    if (!next(&parser)) return 0;
    *out = parse_coalesce(&parser);
    if (!*out || parser.token.kind != DEUS_TOKEN_EOF) {
        if (*out && !diagnostic->message[0]) snprintf(diagnostic->message, sizeof(diagnostic->message), "unexpected token after expression");
        deus_expression_free(*out); *out = NULL; deus_token_dispose(&parser.token); return 0;
    }
    deus_token_dispose(&parser.token); return 1;
}
