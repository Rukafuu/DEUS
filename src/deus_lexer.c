#include "deus_compiler.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void emit_diagnostic(DeusDiagnostic *diagnostic, const DeusLexer *lexer,
                            const char *message) {
    diagnostic->line = lexer->line;
    diagnostic->column = lexer->column;
    snprintf(diagnostic->message, sizeof(diagnostic->message), "%s", message);
}

static void advance(DeusLexer *lexer) {
    char character = *lexer->cursor++;
    if (character == '\r') {
        if (lexer->cursor < lexer->end && *lexer->cursor == '\n') lexer->cursor++;
        lexer->line++; lexer->column = 1u;
    } else if (character == '\n') { lexer->line++; lexer->column = 1u; }
    else lexer->column++;
}

static void skip_trivia(DeusLexer *lexer) {
    for (;;) {
        while (lexer->cursor < lexer->end && isspace((unsigned char)*lexer->cursor)) advance(lexer);
        if (lexer->cursor < lexer->end && *lexer->cursor == '#') {
            while (lexer->cursor < lexer->end && *lexer->cursor != '\n' && *lexer->cursor != '\r') advance(lexer);
        } else if ((size_t)(lexer->end - lexer->cursor) >= 2u &&
                   lexer->cursor[0] == '/' && lexer->cursor[1] == '/') {
            while (lexer->cursor < lexer->end && *lexer->cursor != '\n' && *lexer->cursor != '\r') advance(lexer);
        } else break;
    }
}

void deus_lexer_init(DeusLexer *lexer, const char *source, size_t length) {
    *lexer = (DeusLexer){source, source + length, 1u, 1u};
}

void deus_token_dispose(DeusToken *token) {
    if (!token) return;
    free(token->owned);
    memset(token, 0, sizeof(*token));
}

static int lex_string(DeusLexer *lexer, DeusToken *token, DeusDiagnostic *diagnostic) {
    size_t capacity = 32u, used = 0u;
    char *value = (char *)malloc(capacity);
    if (!value) { emit_diagnostic(diagnostic, lexer, "out of memory"); return 0; }
    advance(lexer);
    while (lexer->cursor < lexer->end && *lexer->cursor != '"') {
        char character = *lexer->cursor;
        if (character == '\n' || character == '\r') {
            free(value); emit_diagnostic(diagnostic, lexer, "newline inside string"); return 0;
        }
        advance(lexer);
        if (character == '\\') {
            if (lexer->cursor >= lexer->end) {
                free(value); emit_diagnostic(diagnostic, lexer, "unfinished escape"); return 0;
            }
            character = *lexer->cursor; advance(lexer);
            if (character == 'n') character = '\n';
            else if (character == 't') character = '\t';
            else if (character != '"' && character != '\\') {
                free(value); emit_diagnostic(diagnostic, lexer, "invalid escape"); return 0;
            }
        }
        if (used + 1u >= capacity) {
            char *next;
            if (capacity > SIZE_MAX / 2u) { free(value); emit_diagnostic(diagnostic, lexer, "string too large"); return 0; }
            capacity *= 2u; next = (char *)realloc(value, capacity);
            if (!next) { free(value); emit_diagnostic(diagnostic, lexer, "out of memory"); return 0; }
            value = next;
        }
        value[used++] = character;
    }
    if (lexer->cursor >= lexer->end) {
        free(value); emit_diagnostic(diagnostic, lexer, "unterminated string"); return 0;
    }
    advance(lexer); value[used] = '\0';
    token->kind = DEUS_TOKEN_STRING; token->owned = value; token->start = value; token->length = used;
    return 1;
}

int deus_lexer_next(DeusLexer *lexer, DeusToken *token, DeusDiagnostic *diagnostic) {
    const char *start;
    memset(token, 0, sizeof(*token)); skip_trivia(lexer);
    token->line = lexer->line; token->column = lexer->column;
    if (lexer->cursor >= lexer->end) { token->kind = DEUS_TOKEN_EOF; return 1; }
    start = lexer->cursor;
    if ((size_t)(lexer->end - lexer->cursor) >= 2u) {
        char first = lexer->cursor[0], second = lexer->cursor[1]; DeusTokenKind compound = DEUS_TOKEN_EOF;
        if (first == '=' && second == '=') compound = DEUS_TOKEN_EQUAL_EQUAL;
        else if (first == '!' && second == '=') compound = DEUS_TOKEN_NOT_EQUAL;
        else if (first == '<' && second == '=') compound = DEUS_TOKEN_LESS_EQUAL;
        else if (first == '>' && second == '=') compound = DEUS_TOKEN_GREATER_EQUAL;
        else if (first == '?' && second == '?') compound = DEUS_TOKEN_COALESCE;
        if (compound != DEUS_TOKEN_EOF) {
            advance(lexer); advance(lexer); token->kind = compound;
            token->start = start; token->length = 2u; return 1;
        }
    }
    if (*lexer->cursor == '=' || *lexer->cursor == '<' || *lexer->cursor == '>' ||
        *lexer->cursor == '(' || *lexer->cursor == ')' || *lexer->cursor == '+' ||
        *lexer->cursor == '-' || *lexer->cursor == '*' || *lexer->cursor == '/') {
        char punctuation = *lexer->cursor; advance(lexer); token->start = start; token->length = 1u;
        token->kind = punctuation == '=' ? DEUS_TOKEN_EQUAL : punctuation == '<' ? DEUS_TOKEN_LESS :
            punctuation == '>' ? DEUS_TOKEN_GREATER : punctuation == '(' ? DEUS_TOKEN_LPAREN :
            punctuation == ')' ? DEUS_TOKEN_RPAREN : punctuation == '+' ? DEUS_TOKEN_PLUS :
            punctuation == '-' ? DEUS_TOKEN_MINUS : punctuation == '*' ? DEUS_TOKEN_STAR :
            DEUS_TOKEN_SLASH;
        return 1;
    }
    if (*lexer->cursor == '{' || *lexer->cursor == '}' || *lexer->cursor == '[' ||
        *lexer->cursor == ']' || *lexer->cursor == ':' || *lexer->cursor == ',' ||
        *lexer->cursor == '%' || *lexer->cursor == '.') {
        char punctuation = *lexer->cursor; advance(lexer); token->start = start; token->length = 1u;
        token->kind = punctuation == '{' ? DEUS_TOKEN_LBRACE : punctuation == '}' ? DEUS_TOKEN_RBRACE :
            punctuation == '[' ? DEUS_TOKEN_LBRACKET : punctuation == ']' ? DEUS_TOKEN_RBRACKET :
            punctuation == ':' ? DEUS_TOKEN_COLON : punctuation == ',' ? DEUS_TOKEN_COMMA :
            punctuation == '%' ? DEUS_TOKEN_PERCENT : DEUS_TOKEN_DOT;
        return 1;
    }
    if (isalpha((unsigned char)*lexer->cursor) || *lexer->cursor == '_') {
        do advance(lexer); while (lexer->cursor < lexer->end &&
            (isalnum((unsigned char)*lexer->cursor) || *lexer->cursor == '_'));
        if (lexer->cursor < lexer->end && *lexer->cursor == '?') advance(lexer);
        token->kind = DEUS_TOKEN_IDENTIFIER; token->start = start;
        token->length = (size_t)(lexer->cursor - start); return 1;
    }
    if (isdigit((unsigned char)*lexer->cursor)) {
        uint64_t value = 0u, limit = (uint64_t)INT64_MAX;
        do {
            uint64_t digit = (uint64_t)(*lexer->cursor - '0');
            if (value > (limit - digit) / 10u) { emit_diagnostic(diagnostic, lexer, "integer exceeds i64"); return 0; }
            value = value * 10u + digit;
            advance(lexer);
        } while (lexer->cursor < lexer->end && isdigit((unsigned char)*lexer->cursor));
        token->kind = DEUS_TOKEN_NUMBER; token->start = start;
        token->length = (size_t)(lexer->cursor - start);
        token->integer = (int64_t)value;
        token->number = value <= UINT32_MAX ? (uint32_t)value : 0u; return 1;
    }
    if (*lexer->cursor == '"') return lex_string(lexer, token, diagnostic);
    emit_diagnostic(diagnostic, lexer, "unexpected character"); return 0;
}
