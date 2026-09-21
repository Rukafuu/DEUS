#ifndef DEUS_SOURCE_AST_H
#define DEUS_SOURCE_AST_H

#include "deus.h"

typedef struct {
    size_t offset;
    unsigned line;
    unsigned column;
} DeusSourcePosition;

typedef struct {
    DeusSourcePosition start;
    DeusSourcePosition end;
} DeusSourceSpan;

typedef enum {
    DEUS_SOURCE_LINE_BLANK,
    DEUS_SOURCE_LINE_COMMENT,
    DEUS_SOURCE_LINE_CONTENT
} DeusSourceLineKind;

typedef enum {
    DEUS_SOURCE_OWNER_NONE,
    DEUS_SOURCE_OWNER_FLOW,
    DEUS_SOURCE_OWNER_LIMITS
} DeusSourceOwner;

typedef struct {
    DeusSourceLineKind kind;
    DeusSourceOwner owner;
    DeusSourceSpan span;
    DeusSourceSpan content_span;
    unsigned indent;
    unsigned depth;
} DeusSourceLogicalLine;

typedef enum {
    DEUS_SOURCE_EVENT_LINE,
    DEUS_SOURCE_EVENT_INDENT,
    DEUS_SOURCE_EVENT_DEDENT,
    DEUS_SOURCE_EVENT_EOF
} DeusSourceLayoutEventKind;

typedef struct {
    DeusSourceLayoutEventKind kind;
    size_t line_index;
    unsigned from_depth;
    unsigned to_depth;
    DeusSourceSpan span;
} DeusSourceLayoutEvent;

typedef enum {
    DEUS_SOURCE_LIMIT_WORKERS,
    DEUS_SOURCE_LIMIT_RETRY,
    DEUS_SOURCE_LIMIT_BACKOFF,
    DEUS_SOURCE_LIMIT_RATE
} DeusSourceLimitKind;

typedef struct {
    DeusSourceLimitKind kind;
    uint32_t value;
    DeusSourceSpan span;
    DeusSourceSpan name_span;
    DeusSourceSpan value_span;
} DeusSourceLimitEntry;

typedef struct {
    DeusSourceSpan span;
    DeusSourceLimitEntry *entries;
    size_t entry_count;
} DeusSourceLimitsBlock;

typedef enum {
    DEUS_SOURCE_FLOW_RAW,
    DEUS_SOURCE_FLOW_LIMITS
} DeusSourceFlowItemKind;

typedef struct {
    DeusSourceFlowItemKind kind;
    DeusSourceSpan span;
    union {
        DeusSourceSpan raw;
        DeusSourceLimitsBlock limits;
    } as;
} DeusSourceFlowItem;

typedef struct {
    DeusSourceSpan span;
    DeusSourceSpan name_span;
    char *name;
    DeusSourceFlowItem *items;
    size_t item_count;
} DeusSourceFlowDecl;

typedef struct {
    char *source;
    size_t source_length;
    DeusSourceLogicalLine *lines;
    size_t line_count;
    DeusSourceLayoutEvent *events;
    size_t event_count;
    DeusSourceFlowDecl flow;
} DeusSourceAst;

void deus_source_ast_free(DeusSourceAst *ast);
const DeusSourceLogicalLine *deus_source_ast_lines(const DeusSourceAst *ast,
                                                   size_t *count);
const DeusSourceLayoutEvent *deus_source_ast_events(const DeusSourceAst *ast,
                                                    size_t *count);

#endif
