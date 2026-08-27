#include "deus_value.h"

#include <stdlib.h>
#include <string.h>

#define DEUS_DEFAULT_VALUE_MEMORY (64u * 1024u * 1024u)
#define DEUS_DEFAULT_VALUE_DEPTH 32u
#define DEUS_DEFAULT_LIST_ITEMS 65536u
#define DEUS_DEFAULT_RECORD_FIELDS 4096u
#define DEUS_DEFAULT_BLOB_BYTES (32u * 1024u * 1024u)

typedef struct {
    char *key;
    size_t key_length;
    DeusValue value;
} DeusRecordField;

struct DeusValueContext {
    DeusValueLimits limits;
    size_t memory_used;
    char error[96];
};

struct DeusValueObject {
    DeusValueKind kind;
    uint32_t references;
    uint32_t depth;
    DeusValueContext *context;
    union {
        struct { unsigned char *data; size_t length; } blob;
        struct { DeusValue *items; size_t count, capacity; } list;
        struct { DeusRecordField *fields; size_t count, capacity; } record;
        struct { unsigned char *body; size_t length; uint32_t status; } document;
        struct { void *payload; DeusValueFinalizer finalizer; } future;
        struct { int64_t code; char *message; size_t length; } error;
    } as;
};

static void set_error(DeusValueContext *context, const char *message) {
    size_t length;
    if (!context) return;
    length = strlen(message);
    if (length >= sizeof(context->error)) length = sizeof(context->error) - 1u;
    memcpy(context->error, message, length);
    context->error[length] = '\0';
}

static void *context_alloc(DeusValueContext *context, size_t bytes) {
    void *allocation;
    if (!context || context->memory_used > context->limits.memory_bytes ||
        bytes > context->limits.memory_bytes - context->memory_used) {
        set_error(context, "value memory limit exceeded");
        return NULL;
    }
    allocation = calloc(1, bytes ? bytes : 1u);
    if (!allocation) {
        set_error(context, "out of memory");
        return NULL;
    }
    context->memory_used += bytes;
    return allocation;
}

static void context_free(DeusValueContext *context, void *memory, size_t bytes) {
    if (!memory) return;
    free(memory);
    if (context && context->memory_used >= bytes) context->memory_used -= bytes;
}

static int utf8_valid(const char *text, size_t length) {
    size_t index = 0;
    while (index < length) {
        unsigned char first = (unsigned char)text[index++];
        uint32_t codepoint;
        size_t remaining;
        if (first < 0x80u) continue;
        if ((first & 0xE0u) == 0xC0u) { codepoint = first & 0x1Fu; remaining = 1u; }
        else if ((first & 0xF0u) == 0xE0u) { codepoint = first & 0x0Fu; remaining = 2u; }
        else if ((first & 0xF8u) == 0xF0u) { codepoint = first & 0x07u; remaining = 3u; }
        else return 0;
        if (index + remaining > length) return 0;
        for (size_t part = 0; part < remaining; part++) {
            unsigned char byte = (unsigned char)text[index++];
            if ((byte & 0xC0u) != 0x80u) return 0;
            codepoint = (codepoint << 6) | (byte & 0x3Fu);
        }
        if ((remaining == 1u && codepoint < 0x80u) ||
            (remaining == 2u && codepoint < 0x800u) ||
            (remaining == 3u && codepoint < 0x10000u) ||
            codepoint > 0x10FFFFu || (codepoint >= 0xD800u && codepoint <= 0xDFFFu)) return 0;
    }
    return 1;
}

static int heap_kind(DeusValueKind kind) {
    return kind >= DEUS_VALUE_STRING;
}

static DeusValueObject *object_create(DeusValueContext *context, DeusValueKind kind) {
    DeusValueObject *object = (DeusValueObject *)context_alloc(context, sizeof(*object));
    if (!object) return NULL;
    object->kind = kind;
    object->references = 1u;
    object->context = context;
    return object;
}

static int blob_create(DeusValueContext *context, DeusValueKind kind,
                       const void *data, size_t length, DeusValue *out) {
    DeusValueObject *object;
    if (!out || (!data && length)) return 0;
    if (length > context->limits.max_blob_bytes) {
        set_error(context, "value blob limit exceeded");
        return 0;
    }
    object = object_create(context, kind);
    if (!object) return 0;
    object->as.blob.data = (unsigned char *)context_alloc(context, length + (kind == DEUS_VALUE_STRING ? 1u : 0u));
    if (!object->as.blob.data) {
        context_free(context, object, sizeof(*object));
        return 0;
    }
    if (length) memcpy(object->as.blob.data, data, length);
    object->as.blob.length = length;
    *out = (DeusValue){kind, {.object = object}};
    return 1;
}

DeusValueLimits deus_value_default_limits(void) {
    DeusValueLimits limits = {DEUS_DEFAULT_VALUE_MEMORY, DEUS_DEFAULT_VALUE_DEPTH,
                              DEUS_DEFAULT_LIST_ITEMS, DEUS_DEFAULT_RECORD_FIELDS,
                              DEUS_DEFAULT_BLOB_BYTES};
    return limits;
}

DeusValueContext *deus_value_context_create(const DeusValueLimits *limits) {
    DeusValueContext *context = (DeusValueContext *)calloc(1, sizeof(*context));
    if (!context) return NULL;
    context->limits = limits ? *limits : deus_value_default_limits();
    if (!context->limits.memory_bytes || !context->limits.max_depth ||
        !context->limits.max_list_items || !context->limits.max_record_fields ||
        !context->limits.max_blob_bytes) {
        free(context);
        return NULL;
    }
    return context;
}

void deus_value_context_destroy(DeusValueContext *context) { free(context); }
size_t deus_value_context_memory_used(const DeusValueContext *context) { return context ? context->memory_used : 0u; }
const char *deus_value_context_error(const DeusValueContext *context) { return context ? context->error : "invalid value context"; }
DeusValue deus_value_null(void) { DeusValue value = {DEUS_VALUE_NULL, {.integer = 0}}; return value; }
DeusValue deus_value_bool(int boolean) { DeusValue value = {DEUS_VALUE_BOOL, {.boolean = boolean != 0}}; return value; }
DeusValue deus_value_i64(int64_t integer) { DeusValue value = {DEUS_VALUE_I64, {.integer = integer}}; return value; }

int deus_value_string(DeusValueContext *context, const char *utf8, size_t length, DeusValue *out) {
    if (!context || !utf8_valid(utf8, length)) { set_error(context, "invalid UTF-8 string"); return 0; }
    return blob_create(context, DEUS_VALUE_STRING, utf8, length, out);
}
int deus_value_bytes(DeusValueContext *context, const void *bytes, size_t length, DeusValue *out) {
    return context ? blob_create(context, DEUS_VALUE_BYTES, bytes, length, out) : 0;
}

static int empty_object(DeusValueContext *context, DeusValueKind kind, DeusValue *out) {
    DeusValueObject *object;
    if (!context || !out) return 0;
    object = object_create(context, kind);
    if (!object) return 0;
    *out = (DeusValue){kind, {.object = object}};
    return 1;
}

int deus_value_list(DeusValueContext *context, DeusValue *out) { return empty_object(context, DEUS_VALUE_LIST, out); }
int deus_value_record(DeusValueContext *context, DeusValue *out) { return empty_object(context, DEUS_VALUE_RECORD, out); }

int deus_value_document(DeusValueContext *context, const void *body, size_t length,
                        uint32_t status, DeusValue *out) {
    DeusValueObject *object;
    if (!context || !out || (!body && length) || length > context->limits.max_blob_bytes) {
        set_error(context, "document body limit exceeded"); return 0;
    }
    object = object_create(context, DEUS_VALUE_DOCUMENT);
    if (!object) return 0;
    object->as.document.body = (unsigned char *)context_alloc(context, length);
    if (length && !object->as.document.body) { context_free(context, object, sizeof(*object)); return 0; }
    if (length) memcpy(object->as.document.body, body, length);
    object->as.document.length = length; object->as.document.status = status;
    *out = (DeusValue){DEUS_VALUE_DOCUMENT, {.object = object}}; return 1;
}

int deus_value_future(DeusValueContext *context, void *payload,
                      DeusValueFinalizer finalizer, DeusValue *out) {
    DeusValueObject *object;
    if (!empty_object(context, DEUS_VALUE_FUTURE, out)) return 0;
    object = out->as.object; object->as.future.payload = payload; object->as.future.finalizer = finalizer;
    return 1;
}

int deus_value_error(DeusValueContext *context, int64_t code,
                     const char *message, size_t length, DeusValue *out) {
    DeusValueObject *object;
    if (!context || !out || !utf8_valid(message, length) || length > context->limits.max_blob_bytes) {
        set_error(context, "invalid error message"); return 0;
    }
    object = object_create(context, DEUS_VALUE_ERROR);
    if (!object) return 0;
    object->as.error.message = (char *)context_alloc(context, length + 1u);
    if (!object->as.error.message) { context_free(context, object, sizeof(*object)); return 0; }
    memcpy(object->as.error.message, message, length); object->as.error.message[length] = '\0';
    object->as.error.length = length; object->as.error.code = code;
    *out = (DeusValue){DEUS_VALUE_ERROR, {.object = object}}; return 1;
}

void deus_value_copy(DeusValue *destination, const DeusValue *source) {
    if (!destination || !source) return;
    *destination = *source;
    if (heap_kind(source->kind) && source->as.object) source->as.object->references++;
}

void deus_value_move(DeusValue *destination, DeusValue *source) {
    if (!destination || !source) return;
    *destination = *source; *source = deus_value_null();
}

void deus_value_dispose(DeusValue *value) {
    DeusValueObject *object;
    DeusValueContext *context;
    if (!value || !heap_kind(value->kind) || !value->as.object) { if (value) *value = deus_value_null(); return; }
    object = value->as.object; *value = deus_value_null();
    if (--object->references) return;
    context = object->context;
    if (object->kind == DEUS_VALUE_STRING || object->kind == DEUS_VALUE_BYTES)
        context_free(context, object->as.blob.data, object->as.blob.length + (object->kind == DEUS_VALUE_STRING ? 1u : 0u));
    else if (object->kind == DEUS_VALUE_LIST) {
        for (size_t i = 0; i < object->as.list.count; i++) deus_value_dispose(&object->as.list.items[i]);
        context_free(context, object->as.list.items, object->as.list.capacity * sizeof(*object->as.list.items));
    } else if (object->kind == DEUS_VALUE_RECORD) {
        for (size_t i = 0; i < object->as.record.count; i++) {
            context_free(context, object->as.record.fields[i].key, object->as.record.fields[i].key_length + 1u);
            deus_value_dispose(&object->as.record.fields[i].value);
        }
        context_free(context, object->as.record.fields, object->as.record.capacity * sizeof(*object->as.record.fields));
    } else if (object->kind == DEUS_VALUE_DOCUMENT)
        context_free(context, object->as.document.body, object->as.document.length);
    else if (object->kind == DEUS_VALUE_FUTURE && object->as.future.finalizer)
        object->as.future.finalizer(object->as.future.payload);
    else if (object->kind == DEUS_VALUE_ERROR)
        context_free(context, object->as.error.message, object->as.error.length + 1u);
    context_free(context, object, sizeof(*object));
}

static uint32_t value_depth(const DeusValue *value) {
    return value && heap_kind(value->kind) && value->as.object ? value->as.object->depth : 0u;
}

static int value_reaches_object(const DeusValue *value, const DeusValueObject *target) {
    const DeusValueObject *object;
    if (!value || !heap_kind(value->kind) || !value->as.object) return 0;
    object = value->as.object;
    if (object == target) return 1;
    if (object->kind == DEUS_VALUE_LIST) {
        for (size_t i = 0; i < object->as.list.count; i++)
            if (value_reaches_object(&object->as.list.items[i], target)) return 1;
    } else if (object->kind == DEUS_VALUE_RECORD) {
        for (size_t i = 0; i < object->as.record.count; i++)
            if (value_reaches_object(&object->as.record.fields[i].value, target)) return 1;
    }
    return 0;
}

static int can_attach(DeusValueObject *container, const DeusValue *child) {
    if (heap_kind(child->kind) && child->as.object && child->as.object->context != container->context) {
        set_error(container->context, "cannot mix value contexts");
        return 0;
    }
    if (value_reaches_object(child, container)) {
        set_error(container->context, "cyclic values are not allowed");
        return 0;
    }
    return 1;
}

static int grow_array(DeusValueContext *context, void **items, size_t item_size,
                      size_t count, size_t *capacity) {
    size_t next_capacity = *capacity ? *capacity * 2u : 4u;
    void *next = context_alloc(context, next_capacity * item_size);
    if (!next) return 0;
    if (count) memcpy(next, *items, count * item_size);
    context_free(context, *items, *capacity * item_size);
    *items = next; *capacity = next_capacity; return 1;
}

/* Collections are exposed as reference-counted values. Detach before a
 * mutation so an API-level copy remains a stable snapshot. */
static int detach_collection(DeusValue *value) {
    DeusValueObject *source, *copy;
    size_t count, index;
    if (!value || (value->kind != DEUS_VALUE_LIST && value->kind != DEUS_VALUE_RECORD) ||
        !value->as.object || value->as.object->references == 1u) return 1;
    source = value->as.object;
    copy = object_create(source->context, source->kind);
    if (!copy) return 0;
    copy->depth = source->depth;
    if (source->kind == DEUS_VALUE_LIST) {
        count = source->as.list.count;
        if (count) {
            copy->as.list.items = (DeusValue *)context_alloc(copy->context, count * sizeof(*copy->as.list.items));
            if (!copy->as.list.items) { context_free(copy->context, copy, sizeof(*copy)); return 0; }
            copy->as.list.capacity = count;
            for (index = 0; index < count; index++) {
                deus_value_copy(&copy->as.list.items[index], &source->as.list.items[index]);
                copy->as.list.count++;
            }
        }
    } else {
        count = source->as.record.count;
        if (count) {
            copy->as.record.fields = (DeusRecordField *)context_alloc(copy->context, count * sizeof(*copy->as.record.fields));
            if (!copy->as.record.fields) { context_free(copy->context, copy, sizeof(*copy)); return 0; }
            copy->as.record.capacity = count;
            for (index = 0; index < count; index++) {
                const DeusRecordField *source_field = &source->as.record.fields[index];
                DeusRecordField *copy_field = &copy->as.record.fields[index];
                copy_field->key = (char *)context_alloc(copy->context, source_field->key_length + 1u);
                if (!copy_field->key) {
                    DeusValue partial = {DEUS_VALUE_RECORD, {.object = copy}};
                    deus_value_dispose(&partial);
                    return 0;
                }
                memcpy(copy_field->key, source_field->key, source_field->key_length + 1u);
                copy_field->key_length = source_field->key_length;
                deus_value_copy(&copy_field->value, &source_field->value);
                copy->as.record.count++;
            }
        }
    }
    source->references--;
    value->as.object = copy;
    return 1;
}
int deus_value_list_append(DeusValue *list, const DeusValue *item) {
    DeusValueObject *object; uint32_t depth;
    if (!list || list->kind != DEUS_VALUE_LIST || !list->as.object || !item) return 0;
    object = list->as.object;
    if (!can_attach(object, item)) return 0;
    if (object->as.list.count >= object->context->limits.max_list_items) { set_error(object->context, "list item limit exceeded"); return 0; }
    depth = value_depth(item) + 1u;
    if (depth > object->context->limits.max_depth) { set_error(object->context, "value depth limit exceeded"); return 0; }
    if (!detach_collection(list)) return 0;
    object = list->as.object;
    if (object->as.list.count == object->as.list.capacity &&
        !grow_array(object->context, (void **)&object->as.list.items, sizeof(*object->as.list.items), object->as.list.count, &object->as.list.capacity)) return 0;
    deus_value_copy(&object->as.list.items[object->as.list.count++], item);
    if (depth > object->depth) object->depth = depth;
    return 1;
}

size_t deus_value_list_count(const DeusValue *list) { return list && list->kind == DEUS_VALUE_LIST ? list->as.object->as.list.count : 0u; }
const DeusValue *deus_value_list_at(const DeusValue *list, size_t index) {
    if (!list || list->kind != DEUS_VALUE_LIST || index >= list->as.object->as.list.count) return NULL;
    return &list->as.object->as.list.items[index];
}

int deus_value_record_set(DeusValue *record, const char *key, size_t key_length,
                          const DeusValue *value) {
    DeusValueObject *object; uint32_t depth;
    if (!record || record->kind != DEUS_VALUE_RECORD || !record->as.object || !key || !value) return 0;
    object = record->as.object;
    if (!can_attach(object, value)) return 0;
    if (!utf8_valid(key, key_length)) { set_error(object->context, "invalid UTF-8 record key"); return 0; }
    depth = value_depth(value) + 1u;
    if (depth > object->context->limits.max_depth) { set_error(object->context, "value depth limit exceeded"); return 0; }
    if (!detach_collection(record)) return 0;
    object = record->as.object;
    for (size_t i = 0; i < object->as.record.count; i++) {
        DeusRecordField *field = &object->as.record.fields[i];
        if (field->key_length == key_length && !memcmp(field->key, key, key_length)) {
            DeusValue replacement; deus_value_copy(&replacement, value); deus_value_dispose(&field->value); field->value = replacement;
            if (depth > object->depth) object->depth = depth;
            return 1;
        }
    }
    if (object->as.record.count >= object->context->limits.max_record_fields) { set_error(object->context, "record field limit exceeded"); return 0; }
    if (object->as.record.count == object->as.record.capacity &&
        !grow_array(object->context, (void **)&object->as.record.fields, sizeof(*object->as.record.fields), object->as.record.count, &object->as.record.capacity)) return 0;
    DeusRecordField *field = &object->as.record.fields[object->as.record.count];
    field->key = (char *)context_alloc(object->context, key_length + 1u);
    if (!field->key) return 0;
    memcpy(field->key, key, key_length); field->key[key_length] = '\0'; field->key_length = key_length;
    deus_value_copy(&field->value, value); object->as.record.count++;
    if (depth > object->depth) object->depth = depth;
    return 1;
}

size_t deus_value_record_count(const DeusValue *record) { return record && record->kind == DEUS_VALUE_RECORD ? record->as.object->as.record.count : 0u; }
const DeusValue *deus_value_record_get(const DeusValue *record, const char *key, size_t key_length) {
    if (!record || record->kind != DEUS_VALUE_RECORD || !key) return NULL;
    for (size_t i = 0; i < record->as.object->as.record.count; i++) {
        const DeusRecordField *field = &record->as.object->as.record.fields[i];
        if (field->key_length == key_length && !memcmp(field->key, key, key_length)) return &field->value;
    }
    return NULL;
}

const void *deus_value_data(const DeusValue *value, size_t *length) {
    if (!value || !length || !value->as.object) return NULL;
    if (value->kind == DEUS_VALUE_STRING || value->kind == DEUS_VALUE_BYTES) { *length = value->as.object->as.blob.length; return value->as.object->as.blob.data; }
    if (value->kind == DEUS_VALUE_DOCUMENT) { *length = value->as.object->as.document.length; return value->as.object->as.document.body; }
    return NULL;
}
uint32_t deus_value_document_status(const DeusValue *value) { return value && value->kind == DEUS_VALUE_DOCUMENT ? value->as.object->as.document.status : 0u; }
void *deus_value_future_payload(const DeusValue *value) { return value && value->kind == DEUS_VALUE_FUTURE ? value->as.object->as.future.payload : NULL; }
int64_t deus_value_error_code(const DeusValue *value) { return value && value->kind == DEUS_VALUE_ERROR ? value->as.object->as.error.code : 0; }
const char *deus_value_error_message(const DeusValue *value, size_t *length) {
    if (!value || value->kind != DEUS_VALUE_ERROR || !length) return NULL;
    *length = value->as.object->as.error.length; return value->as.object->as.error.message;
}

static int write_json_string(FILE *output, const unsigned char *data, size_t length) {
    if (fputc('"', output) == EOF) return 0;
    for (size_t index = 0; index < length; index++) {
        unsigned char byte = data[index];
        if (byte == '"' || byte == '\\') { if (fputc('\\', output) == EOF || fputc(byte, output) == EOF) return 0; }
        else if (byte == '\b') { if (fputs("\\b", output) == EOF) return 0; }
        else if (byte == '\f') { if (fputs("\\f", output) == EOF) return 0; }
        else if (byte == '\n') { if (fputs("\\n", output) == EOF) return 0; }
        else if (byte == '\r') { if (fputs("\\r", output) == EOF) return 0; }
        else if (byte == '\t') { if (fputs("\\t", output) == EOF) return 0; }
        else if (byte < 0x20u) { if (fprintf(output, "\\u%04X", (unsigned)byte) < 0) return 0; }
        else if (fputc(byte, output) == EOF) return 0;
    }
    return fputc('"', output) != EOF;
}

static int write_json_value(const DeusValue *value, FILE *output, uint32_t depth) {
    if (!value || depth > DEUS_DEFAULT_VALUE_DEPTH) return 0;
    if (value->kind == DEUS_VALUE_NULL) return fputs("null", output) != EOF;
    if (value->kind == DEUS_VALUE_BOOL) return fputs(value->as.boolean ? "true" : "false", output) != EOF;
    if (value->kind == DEUS_VALUE_I64) return fprintf(output, "%lld", (long long)value->as.integer) >= 0;
    if (value->kind == DEUS_VALUE_STRING)
        return write_json_string(output, value->as.object->as.blob.data, value->as.object->as.blob.length);
    if (value->kind == DEUS_VALUE_BYTES || value->kind == DEUS_VALUE_DOCUMENT ||
        value->kind == DEUS_VALUE_FUTURE || value->kind == DEUS_VALUE_ERROR) return 0;
    if (value->kind == DEUS_VALUE_LIST) {
        if (fputc('[', output) == EOF) return 0;
        for (size_t index = 0; index < value->as.object->as.list.count; index++) {
            if ((index && fputc(',', output) == EOF) ||
                !write_json_value(&value->as.object->as.list.items[index], output, depth + 1u)) return 0;
        }
        return fputc(']', output) != EOF;
    }
    if (value->kind == DEUS_VALUE_RECORD) {
        if (fputc('{', output) == EOF) return 0;
        for (size_t index = 0; index < value->as.object->as.record.count; index++) {
            const DeusRecordField *field = &value->as.object->as.record.fields[index];
            if ((index && fputc(',', output) == EOF) ||
                !write_json_string(output, (const unsigned char *)field->key, field->key_length) ||
                fputc(':', output) == EOF || !write_json_value(&field->value, output, depth + 1u)) return 0;
        }
        return fputc('}', output) != EOF;
    }
    return 0;
}

int deus_value_write_json(const DeusValue *value, FILE *output) {
    return output && write_json_value(value, output, 0u) && !ferror(output);
}
