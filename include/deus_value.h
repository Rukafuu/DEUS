#ifndef DEUS_VALUE_H
#define DEUS_VALUE_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef enum {
    DEUS_VALUE_NULL,
    DEUS_VALUE_BOOL,
    DEUS_VALUE_I64,
    DEUS_VALUE_STRING,
    DEUS_VALUE_BYTES,
    DEUS_VALUE_LIST,
    DEUS_VALUE_RECORD,
    DEUS_VALUE_DOCUMENT,
    DEUS_VALUE_FUTURE,
    DEUS_VALUE_ERROR
} DeusValueKind;

typedef struct DeusValueContext DeusValueContext;
typedef struct DeusValueObject DeusValueObject;

typedef struct {
    DeusValueKind kind;
    union {
        int boolean;
        int64_t integer;
        DeusValueObject *object;
    } as;
} DeusValue;

typedef struct {
    size_t memory_bytes;
    uint32_t max_depth;
    uint32_t max_list_items;
    uint32_t max_record_fields;
    size_t max_blob_bytes;
} DeusValueLimits;

typedef void (*DeusValueFinalizer)(void *payload);
typedef int (*DeusValueWrite)(void *context, const void *data, size_t length);

DeusValueLimits deus_value_default_limits(void);
DeusValueContext *deus_value_context_create(const DeusValueLimits *limits);
void deus_value_context_destroy(DeusValueContext *context);
size_t deus_value_context_memory_used(const DeusValueContext *context);
const char *deus_value_context_error(const DeusValueContext *context);

DeusValue deus_value_null(void);
DeusValue deus_value_bool(int value);
DeusValue deus_value_i64(int64_t value);
int deus_value_string(DeusValueContext *context, const char *utf8, size_t length,
                      DeusValue *out);
int deus_value_bytes(DeusValueContext *context, const void *bytes, size_t length,
                     DeusValue *out);
int deus_value_list(DeusValueContext *context, DeusValue *out);
int deus_value_record(DeusValueContext *context, DeusValue *out);
int deus_value_document(DeusValueContext *context, const void *body, size_t length,
                        uint32_t status, DeusValue *out);
int deus_value_future(DeusValueContext *context, void *payload,
                      DeusValueFinalizer finalizer, DeusValue *out);
int deus_value_error(DeusValueContext *context, int64_t code,
                     const char *message, size_t length, DeusValue *out);

void deus_value_copy(DeusValue *destination, const DeusValue *source);
void deus_value_move(DeusValue *destination, DeusValue *source);
void deus_value_dispose(DeusValue *value);

int deus_value_list_append(DeusValue *list, const DeusValue *item);
size_t deus_value_list_count(const DeusValue *list);
const DeusValue *deus_value_list_at(const DeusValue *list, size_t index);
int deus_value_record_set(DeusValue *record, const char *key, size_t key_length,
                          const DeusValue *value);
size_t deus_value_record_count(const DeusValue *record);
const DeusValue *deus_value_record_get(const DeusValue *record,
                                       const char *key, size_t key_length);

const void *deus_value_data(const DeusValue *value, size_t *length);
uint32_t deus_value_document_status(const DeusValue *value);
void *deus_value_future_payload(const DeusValue *value);
int64_t deus_value_error_code(const DeusValue *value);
const char *deus_value_error_message(const DeusValue *value, size_t *length);
int deus_value_write_json(const DeusValue *value, FILE *output);
int deus_value_write_json_to(const DeusValue *value, DeusValueWrite write,
                             void *context);

#endif
