#include "deus_value.h"

#include <stdio.h>
#include <string.h>

static FILE *open_temporary_file(void) {
#ifdef _MSC_VER
    FILE *file = NULL;
    return tmpfile_s(&file) == 0 ? file : NULL;
#else
    return tmpfile();
#endif
}

static int finalized;
static void finalize(void *payload) { finalized += *(int *)payload; }

static int require(int condition, const char *message) {
    if (!condition) fprintf(stderr, "value test: %s\n", message);
    return condition;
}

int main(void) {
    DeusValueLimits limits = deus_value_default_limits();
    limits.memory_bytes = 4096u; limits.max_depth = 2u;
    limits.max_list_items = 2u; limits.max_record_fields = 2u; limits.max_blob_bytes = 128u;
    DeusValueContext *context = deus_value_context_create(&limits);
    DeusValue text, bytes, list, record, nested, outer, document, future, error, copy;
    const DeusValue *found; size_t length; int payload = 7; FILE *json_output; char json[64] = {0};
    static const unsigned char raw[] = {0u, 1u, 2u};
    if (!require(context != NULL, "context allocation")) return 1;
    if (!require(deus_value_string(context, "Frieren", 7u, &text), "UTF-8 string") ||
        !require(deus_value_bytes(context, raw, sizeof(raw), &bytes), "bytes") ||
        !require(deus_value_list(context, &list), "list") ||
        !require(deus_value_record(context, &record), "record")) return 1;
    if (!require(!deus_value_list_append(&list, &list), "cycle rejection") ||
        !require(deus_value_list_append(&list, &text), "append string") ||
        !require(deus_value_list_append(&list, &bytes), "append bytes") ||
        !require(!deus_value_list_append(&list, &text), "list limit")) return 1;
    if (!require(deus_value_record_set(&record, "title", 5u, &text), "record field") ||
        !require(deus_value_record_set(&record, "score", 5u, &(DeusValue){DEUS_VALUE_I64, {.integer = 95}}), "integer field")) return 1;
    found = deus_value_record_get(&record, "title", 5u);
    if (!require(found && deus_value_data(found, &length) && length == 7u, "record lookup")) return 1;
    json_output = open_temporary_file();
    if (!require(json_output != NULL, "JSON output") ||
        !require(deus_value_write_json(&record, json_output), "record JSON serialization")) return 1;
    rewind(json_output); length = fread(json, 1u, sizeof(json) - 1u, json_output); fclose(json_output);
    if (!require(length == 30u && !memcmp(json, "{\"title\":\"Frieren\",\"score\":95}", 30u), "compact record JSON")) return 1;
    if (!require(deus_value_list(context, &nested), "nested list") ||
        !require(deus_value_list_append(&nested, &list), "depth two") ||
        !require(deus_value_list(context, &outer), "outer list") ||
        !require(!deus_value_list_append(&outer, &nested), "depth limit")) return 1;
    if (!require(deus_value_document(context, "{}", 2u, 200u, &document), "document") ||
        !require(deus_value_document_status(&document) == 200u, "document status") ||
        !require(deus_value_future(context, &payload, finalize, &future), "future") ||
        !require(deus_value_future_payload(&future) == &payload, "future payload") ||
        !require(deus_value_error(context, 42, "partial", 7u, &error), "error") ||
        !require(deus_value_error_code(&error) == 42, "error code")) return 1;
    deus_value_copy(&copy, &text); deus_value_dispose(&text);
    if (!require(deus_value_data(&copy, &length) && length == 7u, "reference-counted copy")) return 1;
    deus_value_dispose(&copy); deus_value_dispose(&bytes); deus_value_dispose(&list);
    deus_value_dispose(&record); deus_value_dispose(&nested); deus_value_dispose(&outer); deus_value_dispose(&document);
    deus_value_dispose(&future); deus_value_dispose(&error);
    if (!require(finalized == 7, "future finalizer") ||
        !require(deus_value_context_memory_used(context) == 0u, "all owned memory released")) return 1;
    deus_value_context_destroy(context);
    return 0;
}
