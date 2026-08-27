#ifndef DEUS_COMPILER_H
#define DEUS_COMPILER_H

#include "deus.h"

typedef enum {
    DEUS_TOKEN_EOF,
    DEUS_TOKEN_IDENTIFIER,
    DEUS_TOKEN_STRING,
    DEUS_TOKEN_NUMBER,
    DEUS_TOKEN_EQUAL,
    DEUS_TOKEN_LBRACE,
    DEUS_TOKEN_RBRACE,
    DEUS_TOKEN_LBRACKET,
    DEUS_TOKEN_RBRACKET,
    DEUS_TOKEN_COLON,
    DEUS_TOKEN_COMMA,
    DEUS_TOKEN_LPAREN,
    DEUS_TOKEN_RPAREN,
    DEUS_TOKEN_EQUAL_EQUAL,
    DEUS_TOKEN_NOT_EQUAL,
    DEUS_TOKEN_LESS,
    DEUS_TOKEN_LESS_EQUAL,
    DEUS_TOKEN_GREATER,
    DEUS_TOKEN_GREATER_EQUAL,
    DEUS_TOKEN_COALESCE,
    DEUS_TOKEN_PLUS,
    DEUS_TOKEN_MINUS,
    DEUS_TOKEN_STAR,
    DEUS_TOKEN_SLASH,
    DEUS_TOKEN_PERCENT,
    DEUS_TOKEN_DOT
} DeusTokenKind;

typedef struct {
    DeusTokenKind kind;
    const char *start;
    size_t length;
    char *owned;
    uint32_t number;
    int64_t integer;
    unsigned line, column;
} DeusToken;

typedef struct {
    const char *cursor, *end;
    unsigned line, column;
} DeusLexer;

typedef enum {
    DEUS_EXPRESSION_LITERAL,
    DEUS_EXPRESSION_LOCAL,
    DEUS_EXPRESSION_UNARY,
    DEUS_EXPRESSION_BINARY,
    DEUS_EXPRESSION_CONVERSION,
    DEUS_EXPRESSION_MEMBER_ACCESS,
    DEUS_EXPRESSION_ITEM_ACCESS
} DeusExpressionNodeKind;

typedef enum {
    DEUS_EXPRESSION_OP_NOT,
    DEUS_EXPRESSION_OP_NEGATE,
    DEUS_EXPRESSION_OP_AND,
    DEUS_EXPRESSION_OP_OR,
    DEUS_EXPRESSION_OP_EQUAL,
    DEUS_EXPRESSION_OP_NOT_EQUAL,
    DEUS_EXPRESSION_OP_LESS,
    DEUS_EXPRESSION_OP_LESS_EQUAL,
    DEUS_EXPRESSION_OP_GREATER,
    DEUS_EXPRESSION_OP_GREATER_EQUAL,
    DEUS_EXPRESSION_OP_COALESCE,
    DEUS_EXPRESSION_OP_ADD,
    DEUS_EXPRESSION_OP_SUBTRACT,
    DEUS_EXPRESSION_OP_MULTIPLY,
    DEUS_EXPRESSION_OP_DIVIDE,
    DEUS_EXPRESSION_OP_MODULO,
    DEUS_EXPRESSION_OP_TEXT,
    DEUS_EXPRESSION_OP_I64,
    DEUS_EXPRESSION_OP_BOOL
} DeusExpressionOperator;

typedef struct DeusExpressionNode DeusExpressionNode;
struct DeusExpressionNode {
    DeusExpressionNodeKind kind;
    DeusExpressionOperator operator_kind;
    int literal_kind;
    union { char *string; int64_t integer; int boolean; } literal;
    uint32_t string_length;
    char *symbol;
    uint32_t symbol_length;
    DeusExpressionNode *object;      /* for member/item access: the container */
    DeusExpressionNode *accessor;    /* for member access: field name; for item access: index expression */
    DeusExpressionNode *left;
    DeusExpressionNode *right;
    unsigned line, column;
};

typedef enum {
    DEUS_AST_OPERAND_NONE,
    DEUS_AST_OPERAND_STRING,
    DEUS_AST_OPERAND_U32
} DeusAstOperandKind;

typedef enum {
    DEUS_AST_INSTRUCTION,
    DEUS_AST_BIND_LOCAL,
    DEUS_AST_LOAD_LOCAL,
    DEUS_AST_SET_FIELD,
    DEUS_AST_PUSH_ITEM
} DeusAstStatementKind;

typedef enum {
    DEUS_AST_EXPRESSION_NONE,
    DEUS_AST_EXPRESSION_STRING,
    DEUS_AST_EXPRESSION_I64,
    DEUS_AST_EXPRESSION_BOOL,
    DEUS_AST_EXPRESSION_NULL,
    DEUS_AST_EXPRESSION_LOCAL,
    DEUS_AST_EXPRESSION_HUNT,
    DEUS_AST_EXPRESSION_REAP,
    DEUS_AST_EXPRESSION_CALL,
    DEUS_AST_EXPRESSION_JSON,
    DEUS_AST_EXPRESSION_RECORD,
    DEUS_AST_EXPRESSION_LIST,
    DEUS_AST_EXPRESSION_GET,
    DEUS_AST_EXPRESSION_AT,
    DEUS_AST_EXPRESSION_GET_OPTIONAL,
    DEUS_AST_EXPRESSION_AT_OPTIONAL
} DeusAstExpressionKind;

typedef struct {
    DeusAstStatementKind statement_kind;
    uint8_t opcode;
    DeusAstOperandKind operand_kind;
    union { char *string; uint32_t number; } operand;
    uint32_t string_length;
    DeusAstExpressionKind expression_kind;
    int64_t expression_integer;
    int expression_boolean;
    char *expression_symbol;
    uint32_t expression_symbol_length;
    char *symbol;
    uint32_t symbol_length;
    DeusExpressionNode *expression;
    unsigned line, column;
} DeusAstInstruction;

typedef struct {
    DeusAstInstruction *instructions;
    uint32_t count;
} DeusAstProgram;

void deus_lexer_init(DeusLexer *lexer, const char *source, size_t length);
int deus_lexer_next(DeusLexer *lexer, DeusToken *token, DeusDiagnostic *diagnostic);
void deus_token_dispose(DeusToken *token);
int deus_parse_ast(const char *source, size_t length, DeusAstProgram *out,
                   DeusDiagnostic *diagnostic);
int deus_parse_ast_fragment(const char *source, size_t length,
                            uint32_t hidden_symbol_base,
                            DeusAstProgram *out,
                            DeusDiagnostic *diagnostic);
void deus_ast_free(DeusAstProgram *program);
void deus_expression_free(DeusExpressionNode *expression);
int deus_parse_expression(const char *source, size_t length, DeusExpressionNode **out,
                          DeusDiagnostic *diagnostic);
int deus_analyze_and_generate(const DeusAstProgram *ast, DeusProgram *out,
                              DeusDiagnostic *diagnostic);

#endif
