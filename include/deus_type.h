#ifndef DEUS_TYPE_H
#define DEUS_TYPE_H

#include <stdint.h>
#include <stddef.h>

typedef enum {
    DEUS_TYPE_KIND_NULL,
    DEUS_TYPE_KIND_BOOL,
    DEUS_TYPE_KIND_I64,
    DEUS_TYPE_KIND_STRING,
    DEUS_TYPE_KIND_DOCUMENT,
    DEUS_TYPE_KIND_FUTURE,
    DEUS_TYPE_KIND_ERROR,
    DEUS_TYPE_KIND_LIST,
    DEUS_TYPE_KIND_RECORD,
    DEUS_TYPE_KIND_OPTIONAL,
    DEUS_TYPE_KIND_DYNAMIC
} DeusTypeKind;

typedef struct DeusField {
    char *name;
    uint32_t name_length;
    struct DeusType *type;
} DeusField;

typedef struct DeusType {
    DeusTypeKind kind;
    struct DeusType *element_type;
    struct DeusType *inner_type;
    DeusField *fields;
    uint32_t field_count;
    uint32_t capabilities;
    int is_nullable;
} DeusType;

DeusType *deus_type_null(void);
DeusType *deus_type_bool(void);
DeusType *deus_type_i64(void);
DeusType *deus_type_string(void);
DeusType *deus_type_document(void);
DeusType *deus_type_future(DeusType *inner);
DeusType *deus_type_error(void);
DeusType *deus_type_list(DeusType *element);
DeusType *deus_type_optional(DeusType *inner);
DeusType *deus_type_record(DeusField *fields, uint32_t count);
DeusType *deus_type_dynamic(void);

void deus_type_free(DeusType *type);
int deus_type_equals(const DeusType *a, const DeusType *b);
int deus_type_is_subtype_of(const DeusType *sub, const DeusType *super);
const char *deus_type_to_string(const DeusType *type, char *buffer, size_t capacity);

#endif
