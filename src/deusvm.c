#include "deus.h"
#include "deus_json.h"
#include "deus_value.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#endif

#define STACK_MAX 1024u
#define RESPONSE_MAX (32u * 1024u * 1024u)
#define URL_MAX 8192u

typedef struct Runtime Runtime;
typedef struct HuntTask HuntTask;
typedef enum { V_DOCUMENT, V_TEXT, V_STRING, V_NULL, V_BOOL, V_I64, V_MANAGED, V_FUTURE } ValueKind;
typedef struct { ValueKind kind; char *data; size_t len; HuntTask *future; int64_t scalar; DeusValue managed; } Value;

struct Runtime {
    HINTERNET session;
    HANDLE slots;
    CRITICAL_SECTION rate_lock;
    ULONGLONG next_start;
    uint32_t limit, retries, backoff_ms, rate;
    int locked;
    const DeusHost *host;
};

struct HuntTask {
    Runtime *runtime;
    PTP_WORK work;
    HANDLE done;
    char *url, *body;
    size_t body_len;
    char error[192];
};

static int fail(const char *message) { fprintf(stderr, "deusvm: %s\n", message); return 1; }

static wchar_t *widen(const char *source) {
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, source, -1, NULL, 0);
    if (!n) return NULL;
    wchar_t *wide = (wchar_t *)malloc((size_t)n * sizeof(*wide));
    if (!wide) return NULL;
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, source, -1, wide, n)) {
        free(wide); return NULL;
    }
    return wide;
}

static int runtime_start(Runtime *rt, char *error, size_t cap) {
    if (rt->locked) return 1;
    if (!rt->host) {
        rt->session = WinHttpOpen(L"DEUS/0.2", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!rt->session) { snprintf(error, cap, "WinHTTP session failed"); return 0; }
        WinHttpSetTimeouts(rt->session, 10000, 10000, 30000, 30000);
    }
    rt->slots = CreateSemaphoreW(NULL, (LONG)rt->limit, (LONG)rt->limit, NULL);
    if (!rt->slots) {
        if (rt->session) WinHttpCloseHandle(rt->session); rt->session = NULL;
        snprintf(error, cap, "executor semaphore failed"); return 0;
    }
    rt->locked = 1;
    return 1;
}

static void rate_wait(Runtime *rt) {
    if (!rt->rate) return;
    DWORD wait = 0;
    EnterCriticalSection(&rt->rate_lock);
    ULONGLONG now = GetTickCount64();
    ULONGLONG start = now > rt->next_start ? now : rt->next_start;
    if (start > now) wait = (DWORD)(start - now);
    rt->next_start = start + (1000u + rt->rate - 1u) / rt->rate;
    LeaveCriticalSection(&rt->rate_lock);
    if (wait) Sleep(wait);
}

static char *native_hunt_once(Runtime *rt, const char *url, size_t *out_len,
                              char *error, size_t cap) {
    wchar_t *wurl = widen(url);
    if (!wurl) { snprintf(error, cap, "invalid UTF-8 URL"); return NULL; }
    URL_COMPONENTS u = {sizeof(u)};
    u.dwSchemeLength = u.dwHostNameLength = u.dwUrlPathLength = u.dwExtraInfoLength = (DWORD)-1;
    if (!WinHttpCrackUrl(wurl, 0, 0, &u)) {
        free(wurl); snprintf(error, cap, "invalid URL"); return NULL;
    }
    wchar_t *host = (wchar_t *)calloc((size_t)u.dwHostNameLength + 1, sizeof(wchar_t));
    wchar_t *path = (wchar_t *)calloc((size_t)u.dwUrlPathLength + u.dwExtraInfoLength + 1, sizeof(wchar_t));
    if (!host || !path) {
        free(host); free(path); free(wurl); snprintf(error, cap, "out of memory"); return NULL;
    }
    wmemcpy(host, u.lpszHostName, u.dwHostNameLength);
    wmemcpy(path, u.lpszUrlPath, u.dwUrlPathLength);
    if (u.dwExtraInfoLength) wmemcpy(path + u.dwUrlPathLength, u.lpszExtraInfo, u.dwExtraInfoLength);

    HINTERNET connect = WinHttpConnect(rt->session, host, u.nPort, 0), request = NULL;
    char *body = NULL; size_t used = 0, capacity = 0;
    if (!connect) { snprintf(error, cap, "connection failed (%lu)", GetLastError()); goto done; }
    DWORD flags = u.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
    request = WinHttpOpenRequest(connect, L"GET", path[0] ? path : L"/", NULL,
                                 WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!request) { snprintf(error, cap, "request creation failed"); goto done; }
    DWORD decompression = WINHTTP_DECOMPRESSION_FLAG_GZIP | WINHTTP_DECOMPRESSION_FLAG_DEFLATE;
    WinHttpSetOption(request, WINHTTP_OPTION_DECOMPRESSION, &decompression, sizeof(decompression));
    if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request, NULL)) {
        snprintf(error, cap, "HTTP request failed (%lu)", GetLastError()); goto done;
    }
    DWORD status = 0, status_size = sizeof(status);
    WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size, WINHTTP_NO_HEADER_INDEX);
    if (status < 200 || status >= 300) { snprintf(error, cap, "HTTP status %lu", status); goto done; }
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request, &available)) { snprintf(error, cap, "response read failed"); free(body); body = NULL; goto done; }
        if (!available) break;
        if (used + available > RESPONSE_MAX) { snprintf(error, cap, "response exceeds 32 MiB"); free(body); body = NULL; goto done; }
        if (used + available + 1 > capacity) {
            capacity = (used + available + 1) * 2;
            char *next = (char *)realloc(body, capacity);
            if (!next) { free(body); body = NULL; snprintf(error, cap, "out of memory"); goto done; }
            body = next;
        }
        DWORD got = 0;
        if (!WinHttpReadData(request, body + used, available, &got)) { free(body); body = NULL; snprintf(error, cap, "response read failed"); goto done; }
        used += got;
    }
    if (!body) body = (char *)malloc(1);
    if (body) { body[used] = 0; *out_len = used; }
    else snprintf(error, cap, "out of memory");
done:
    if (request) WinHttpCloseHandle(request);
    if (connect) WinHttpCloseHandle(connect);
    free(host); free(path); free(wurl);
    return body;
}

static char *host_hunt_once(Runtime *rt, const char *url, size_t *out_len,
                            char *error, size_t cap) {
    DeusHostDocument document = {0}; char *copy = NULL;
    if (!rt->host) return native_hunt_once(rt, url, out_len, error, cap);
    if (rt->host->abi_version != DEUS_HOST_ABI_VERSION ||
        !(rt->host->capabilities & DEUS_HOST_CAP_NETWORK) || !rt->host->hunt) {
        snprintf(error, cap, "host does not grant network capability"); return NULL;
    }
    if (cap) error[0] = '\0';
    if (!rt->host->hunt(rt->host->context, url, strlen(url), &document, error, cap)) {
        if (cap && !error[0]) snprintf(error, cap, "host hunt failed");
        return NULL;
    }
    if ((!document.data && document.length) || document.length > RESPONSE_MAX ||
        document.status < 200u || document.status >= 300u) {
        snprintf(error, cap, document.length > RESPONSE_MAX ? "host response exceeds 32 MiB" :
                 (document.status < 200u || document.status >= 300u ? "host returned non-success status" : "host returned invalid document"));
    } else {
        copy = (char *)malloc(document.length + 1u);
        if (copy) {
            if (document.length) memcpy(copy, document.data, document.length);
            copy[document.length] = '\0'; *out_len = document.length;
        } else snprintf(error, cap, "out of memory");
    }
    if (rt->host->release_document) rt->host->release_document(rt->host->context, &document);
    return copy;
}

static char *runtime_hunt(Runtime *rt, const char *url, size_t *len,
                          char *error, size_t cap) {
    if (!runtime_start(rt, error, cap)) return NULL;
    WaitForSingleObject(rt->slots, INFINITE);
    char *body = NULL;
    for (uint32_t attempt = 0; attempt <= rt->retries; attempt++) {
        rate_wait(rt);
        body = host_hunt_once(rt, url, len, error, cap);
        if (body) break;
        if (attempt < rt->retries && rt->backoff_ms) {
            uint32_t shift = attempt > 10 ? 10 : attempt;
            uint64_t delay = (uint64_t)rt->backoff_ms << shift;
            Sleep((DWORD)(delay > 60000u ? 60000u : delay));
        }
    }
    ReleaseSemaphore(rt->slots, 1, NULL);
    return body;
}

static VOID CALLBACK hunt_callback(PTP_CALLBACK_INSTANCE instance, PVOID context, PTP_WORK work) {
    (void)instance; (void)work;
    HuntTask *task = (HuntTask *)context;
    task->body = runtime_hunt(task->runtime, task->url, &task->body_len,
                              task->error, sizeof(task->error));
    SetEvent(task->done);
}

static HuntTask *task_submit(Runtime *rt, const char *url, char *error, size_t cap) {
    if (!runtime_start(rt, error, cap)) return NULL;
    HuntTask *task = (HuntTask *)calloc(1, sizeof(*task));
    if (!task) { snprintf(error, cap, "out of memory"); return NULL; }
    size_t n = strlen(url) + 1;
    task->url = (char *)malloc(n); task->runtime = rt;
    task->done = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (task->url) memcpy(task->url, url, n);
    if (!task->url || !task->done) { free(task->url); if (task->done) CloseHandle(task->done); free(task); snprintf(error, cap, "future allocation failed"); return NULL; }
    task->work = CreateThreadpoolWork(hunt_callback, task, NULL);
    if (!task->work) { CloseHandle(task->done); free(task->url); free(task); snprintf(error, cap, "thread-pool submission failed"); return NULL; }
    SubmitThreadpoolWork(task->work);
    return task;
}

static int task_resolve(HuntTask *task, Value *out, char *error, size_t cap) {
    WaitForSingleObject(task->done, INFINITE);
    WaitForThreadpoolWorkCallbacks(task->work, FALSE);
    CloseThreadpoolWork(task->work); CloseHandle(task->done); free(task->url);
    if (!task->body) { snprintf(error, cap, "%s", task->error[0] ? task->error : "asynchronous HUNT failed"); free(task); return 0; }
    *out = (Value){V_DOCUMENT, task->body, task->body_len, NULL};
    free(task); return 1;
}

static int token_has(const char *value, size_t n, const char *token, size_t tn) {
    size_t i = 0;
    while (i < n) {
        while (i < n && (value[i] == ' ' || value[i] == '\t')) i++;
        size_t begin = i;
        while (i < n && value[i] != ' ' && value[i] != '\t' && value[i] != '"' && value[i] != '\'') i++;
        if (i - begin == tn && !memcmp(value + begin, token, tn)) return 1;
    }
    return 0;
}

static int attr_match(const char *tag, size_t n, const char *name, const char *value, int classes) {
    size_t nl = strlen(name), vl = strlen(value);
    for (size_t i = 0; i + nl < n; i++) {
        if ((i == 0 || tag[i - 1] == ' ' || tag[i - 1] == '\t') && !_strnicmp(tag + i, name, nl)) {
            size_t j = i + nl;
            while (j < n && (tag[j] == ' ' || tag[j] == '\t')) j++;
            if (j >= n || tag[j++] != '=') continue;
            while (j < n && (tag[j] == ' ' || tag[j] == '\t')) j++;
            char q = (j < n && (tag[j] == '"' || tag[j] == '\'')) ? tag[j++] : 0;
            size_t begin = j;
            while (j < n && ((q && tag[j] != q) || (!q && tag[j] != ' ' && tag[j] != '>'))) j++;
            return classes ? token_has(tag + begin, j - begin, value, vl)
                           : (j - begin == vl && !memcmp(tag + begin, value, vl));
        }
    }
    return 0;
}

static int tag_name_match(const char *html, size_t begin, size_t end,
                          const char *name, size_t name_length) {
    size_t cursor = begin;
    while (cursor < end && (html[cursor] == ' ' || html[cursor] == '\t')) cursor++;
    size_t name_end = cursor;
    while (name_end < end && isalnum((unsigned char)html[name_end])) name_end++;
    return name_end - cursor == name_length &&
           !_strnicmp(html + cursor, name, name_length);
}

static int reap_append(char **out, size_t *capacity, size_t *used,
                       const char *text, size_t text_length) {
    if (!text_length) return 1;
    if (*used + text_length + 2u > *capacity) {
        size_t next_capacity = *capacity;
        while (*used + text_length + 2u > next_capacity) next_capacity *= 2u;
        char *next = (char *)realloc(*out, next_capacity);
        if (!next) return 0;
        *out = next; *capacity = next_capacity;
    }
    memcpy(*out + *used, text, text_length); *used += text_length;
    return 1;
}

static char *reap(const char *html, size_t len, const char *selector, size_t *out_len) {
    char mode = 0; const char *needle = selector;
    if (*needle == '#' || *needle == '.') mode = *needle++;
    size_t nn = strlen(needle), cap = 256, used = 0;
    char *out = (char *)malloc(cap); if (!out) return NULL;
    for (size_t i = 0; i < len;) {
        if (html[i] != '<' || i + 2 >= len || html[i + 1] == '/' || html[i + 1] == '!') { i++; continue; }
        size_t close = i + 1; while (close < len && html[close] != '>') close++;
        if (close >= len) break;
        size_t name = i + 1; while (name < close && (html[name] == ' ' || html[name] == '\t')) name++;
        size_t ne = name; while (ne < close && (isalnum((unsigned char)html[ne]))) ne++;
        int match = mode == '#' ? attr_match(html + i + 1, close - i - 1, "id", needle, 0)
                  : mode == '.' ? attr_match(html + i + 1, close - i - 1, "class", needle, 1)
                  : (ne - name == nn && !_strnicmp(html + name, needle, nn));
        if (!match) { i = close + 1; continue; }
        size_t cursor = close + 1, text_begin = cursor, depth = 1;
        while (cursor < len && depth) {
            if (html[cursor] != '<') { cursor++; continue; }
            if (!reap_append(&out, &cap, &used, html + text_begin, cursor - text_begin)) {
                free(out); return NULL;
            }
            size_t tag_close = cursor + 1;
            while (tag_close < len && html[tag_close] != '>') tag_close++;
            if (tag_close >= len) { cursor = len; break; }
            size_t tag_begin = cursor + 1;
            int closing = tag_begin < tag_close && html[tag_begin] == '/';
            if (closing) tag_begin++;
            if (tag_name_match(html, tag_begin, tag_close, html + name, ne - name)) {
                size_t tail = tag_close;
                while (tail > tag_begin && (html[tail - 1] == ' ' || html[tail - 1] == '\t')) tail--;
                if (closing) depth--;
                else if (tail == tag_begin || html[tail - 1] != '/') depth++;
            }
            cursor = tag_close + 1; text_begin = cursor;
        }
        if (depth && text_begin < len &&
            !reap_append(&out, &cap, &used, html + text_begin, len - text_begin)) {
            free(out); return NULL;
        }
        if (used && out[used - 1] != '\n') {
            if (!reap_append(&out, &cap, &used, "\n", 1u)) { free(out); return NULL; }
        }
        i = cursor;
    }
    out[used] = 0; *out_len = used; return out;
}

static void value_dispose(Value *value) {
    if (value->kind == V_FUTURE && value->future) {
        Value resolved; char ignored[192];
        if (task_resolve(value->future, &resolved, ignored, sizeof(ignored))) free(resolved.data);
    } else if (value->kind == V_MANAGED) deus_value_dispose(&value->managed);
    else free(value->data);
    memset(value, 0, sizeof(*value));
}

static int value_clone(const Value *source, Value *out) {
    char *copy;
    if (source->kind == V_FUTURE) return 0;
    if (source->kind == V_MANAGED) { *out = *source; deus_value_copy(&out->managed, &source->managed); return 1; }
    if (source->kind == V_NULL || source->kind == V_BOOL || source->kind == V_I64) {
        *out = *source; return 1;
    }
    copy = (char *)malloc(source->len + 1u);
    if (!copy) return 0;
    if (source->len) memcpy(copy, source->data, source->len);
    copy[source->len] = '\0';
    *out = (Value){source->kind, copy, source->len, NULL}; return 1;
}

static int value_as_managed(const Value *source, DeusValueContext *context, DeusValue *out) {
    if (source->kind == V_MANAGED) { deus_value_copy(out, &source->managed); return 1; }
    if (source->kind == V_NULL) { *out = deus_value_null(); return 1; }
    if (source->kind == V_BOOL) { *out = deus_value_bool(source->scalar != 0); return 1; }
    if (source->kind == V_I64) { *out = deus_value_i64(source->scalar); return 1; }
    if (source->kind == V_STRING || source->kind == V_TEXT)
        return deus_value_string(context, source->data, source->len, out);
    return 0;
}

static int value_from_managed(const DeusValue *source, Value *out) {
    size_t length = 0u; const void *data; char *copy;
    memset(out, 0, sizeof(*out));
    if (source->kind == DEUS_VALUE_NULL) { out->kind = V_NULL; return 1; }
    if (source->kind == DEUS_VALUE_BOOL) { out->kind = V_BOOL; out->scalar = source->as.boolean; return 1; }
    if (source->kind == DEUS_VALUE_I64) { out->kind = V_I64; out->scalar = source->as.integer; return 1; }
    if (source->kind == DEUS_VALUE_STRING) {
        data = deus_value_data(source, &length); copy = (char *)malloc(length + 1u);
        if (!copy) return 0;
        if (length) memcpy(copy, data, length); copy[length] = '\0';
        out->kind = V_STRING; out->data = copy; out->len = length; return 1;
    }
    if (source->kind == DEUS_VALUE_LIST || source->kind == DEUS_VALUE_RECORD) {
        out->kind = V_MANAGED; deus_value_copy(&out->managed, source); return 1;
    }
    return 0;
}

static int runtime_call_adapter(const Runtime *runtime,const char *name,size_t length,const Value *input,DeusValueContext *context,Value *output,char *error,size_t cap){DeusValue value=deus_value_null(),result=deus_value_null();int ok;if(!runtime->host||runtime->host->abi_version!=DEUS_HOST_ABI_VERSION||!(runtime->host->capabilities&DEUS_HOST_CAP_ADAPTER_CALL)||!runtime->host->call){snprintf(error,cap,"runtime requires DeusHost adapter capability");return 0;}if(!value_as_managed(input,context,&value)){snprintf(error,cap,"adapter input is not serializable");return 0;}ok=runtime->host->call(runtime->host->context,name,length,&value,context,&result,error,cap);deus_value_dispose(&value);if(!ok){if(cap&&!error[0])snprintf(error,cap,"host adapter call failed");deus_value_dispose(&result);return 0;}if(!value_from_managed(&result,output)){snprintf(error,cap,"host adapter returned a non-serializable value");deus_value_dispose(&result);return 0;}deus_value_dispose(&result);return 1;}
static int url_unreserved(unsigned char byte) {
    return (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z') ||
           (byte >= '0' && byte <= '9') || byte == '-' || byte == '.' || byte == '_' || byte == '~';
}

static int value_url_encode(Value *value) {
    char scalar[32]; const char *source = value->data; size_t length = value->len;
    if (value->kind == V_I64) {
        int written = snprintf(scalar, sizeof(scalar), "%lld", (long long)value->scalar);
        if (written < 0) return 0; source = scalar; length = (size_t)written;
    } else if (value->kind == V_BOOL) {
        source = value->scalar ? "true" : "false"; length = value->scalar ? 4u : 5u;
    } else if (value->kind != V_STRING && value->kind != V_TEXT) return 0;
    if (length > URL_MAX / 3u) return 0;
    char *encoded = (char *)malloc(length * 3u + 1u); size_t used = 0u;
    if (!encoded) return 0;
    static const char HEX[] = "0123456789ABCDEF";
    for (size_t index = 0; index < length; index++) {
        unsigned char byte = (unsigned char)source[index];
        if (url_unreserved(byte)) encoded[used++] = (char)byte;
        else { encoded[used++] = '%'; encoded[used++] = HEX[byte >> 4]; encoded[used++] = HEX[byte & 15u]; }
    }
    encoded[used] = '\0'; value_dispose(value);
    *value = (Value){V_STRING, encoded, used, NULL, 0}; return 1;
}

int deus_vm_execute_program_with_host(const DeusProgram *input, FILE *output,
                                      const DeusHost *host) {
    DeusProgram p = *input; char error[192];
    if (!deus_validate_program(input, error, sizeof(error))) return fail(error);
    Runtime rt = {0}; rt.limit = 8; rt.retries = 2; rt.backoff_ms = 100; rt.host = host; InitializeCriticalSection(&rt.rate_lock);
    Value stack[STACK_MAX] = {0}; size_t sp = 0; int began = 0, rc = 0;
    Value locals[DEUS_MAX_LOCALS] = {0}; unsigned char local_bound[DEUS_MAX_LOCALS] = {0};
    DeusValueContext *value_context = deus_value_context_create(NULL);
    if (!value_context) { DeleteCriticalSection(&rt.rate_lock); return fail("value context allocation failed"); }

    for (uint32_t pc = 0; pc < p.code_count; pc++) {
        DeusInstruction in = p.code[pc];
        const char *arg = in.operand < p.string_count ? p.strings[in.operand].data : "";
        size_t arg_length = in.operand < p.string_count ? p.strings[in.operand].len : 0u;
        if (in.opcode == DEUS_OMNI) {
            if (strcmp(arg, "net.http2")) { rc = fail("unknown omni module"); break; }
        } else if (in.opcode == DEUS_GENESIS) began = 1;
        else if (in.opcode == DEUS_LIMIT || in.opcode == DEUS_RETRY || in.opcode == DEUS_BACKOFF || in.opcode == DEUS_RATE) {
            if (rt.locked) { rc = fail("executor configuration after network start"); break; }
            if (in.opcode == DEUS_LIMIT) rt.limit = in.operand;
            else if (in.opcode == DEUS_RETRY) rt.retries = in.operand;
            else if (in.opcode == DEUS_BACKOFF) rt.backoff_ms = in.operand;
            else rt.rate = in.operand;
        } else if (in.opcode == DEUS_HUNT) {
            if (!began || sp == STACK_MAX) { rc = fail("invalid VM state at HUNT"); break; }
            size_t len; char *body = runtime_hunt(&rt, arg, &len, error, sizeof(error));
            if (!body) { rc = fail(error); break; }
            stack[sp++] = (Value){V_DOCUMENT, body, len, NULL};
        } else if (in.opcode == DEUS_FORK) {
            if (!began || sp == STACK_MAX) { rc = fail("invalid VM state at FORK"); break; }
            HuntTask *task = task_submit(&rt, arg, error, sizeof(error));
            if (!task) { rc = fail(error); break; }
            stack[sp++] = (Value){V_FUTURE, NULL, 0, task};
        } else if (in.opcode == DEUS_CONST) {
            char *copy;
            if (!began || sp == STACK_MAX || in.operand >= p.string_count) { rc = fail("invalid VM state at CONST"); break; }
            copy = (char *)malloc(arg_length + 1u);
            if (!copy) { rc = fail("CONST allocation failed"); break; }
            if (arg_length) memcpy(copy, arg, arg_length); copy[arg_length] = '\0';
            stack[sp++] = (Value){V_STRING, copy, arg_length, NULL};
        } else if (in.opcode == DEUS_CONST_NULL) {
            if (!began || sp == STACK_MAX) { rc = fail("invalid VM state at CONST_NULL"); break; }
            stack[sp++] = (Value){V_NULL, NULL, 0u, NULL, 0};
        } else if (in.opcode == DEUS_CONST_BOOL) {
            if (!began || sp == STACK_MAX || in.operand > 1u) { rc = fail("invalid VM state at CONST_BOOL"); break; }
            stack[sp++] = (Value){V_BOOL, NULL, 0u, NULL, in.operand ? 1 : 0};
        } else if (in.opcode == DEUS_CONST_I64) {
            if (!began || sp == STACK_MAX) { rc = fail("invalid VM state at CONST_I64"); break; }
            stack[sp++] = (Value){V_I64, NULL, 0u, NULL, in.immediate};
        } else if (in.opcode == DEUS_CONST_RECORD || in.opcode == DEUS_CONST_LIST) {
            DeusValue managed;
            if (!began || sp == STACK_MAX ||
                !(in.opcode == DEUS_CONST_RECORD ? deus_value_record(value_context, &managed) : deus_value_list(value_context, &managed))) {
                rc = fail("compound value allocation failed"); break;
            }
            stack[sp] = (Value){0}; stack[sp].kind = V_MANAGED; stack[sp++].managed = managed;
        } else if (in.opcode == DEUS_RECORD_SET || in.opcode == DEUS_LIST_PUSH) {
            DeusValue item; Value *container, *source;
            if (sp < 2u) { rc = fail("compound mutation requires container and value"); break; }
            container = &stack[sp - 2u]; source = &stack[sp - 1u];
            if (container->kind != V_MANAGED ||
                (in.opcode == DEUS_RECORD_SET && container->managed.kind != DEUS_VALUE_RECORD) ||
                (in.opcode == DEUS_LIST_PUSH && container->managed.kind != DEUS_VALUE_LIST) ||
                !value_as_managed(source, value_context, &item)) { rc = fail("invalid compound mutation"); break; }
            int changed = in.opcode == DEUS_RECORD_SET ?
                deus_value_record_set(&container->managed, arg, arg_length, &item) :
                deus_value_list_append(&container->managed, &item);
            deus_value_dispose(&item);
            if (!changed) { rc = fail(deus_value_context_error(value_context)); break; }
            value_dispose(source); value_dispose(container); sp -= 2u;
        } else if (in.opcode == DEUS_RECORD_GET || in.opcode == DEUS_LIST_AT ||
                   in.opcode == DEUS_RECORD_GET_OPTIONAL || in.opcode == DEUS_LIST_AT_OPTIONAL) {
            const DeusValue *found; Value extracted;
            int is_get = in.opcode == DEUS_RECORD_GET || in.opcode == DEUS_RECORD_GET_OPTIONAL;
            int is_optional = in.opcode == DEUS_RECORD_GET_OPTIONAL || in.opcode == DEUS_LIST_AT_OPTIONAL;
            if (!sp || stack[sp - 1u].kind != V_MANAGED ||
                (is_get && stack[sp - 1u].managed.kind != DEUS_VALUE_RECORD) ||
                (!is_get && stack[sp - 1u].managed.kind != DEUS_VALUE_LIST)) {
                if (is_optional && sp) {
                    value_dispose(&stack[sp - 1u]);
                    stack[sp - 1u] = (Value){V_NULL, NULL, 0u, NULL, 0};
                    continue;
                }
                rc = fail(is_get ? "RECORD_GET requires a record" : "LIST_AT requires a list");
                break;
            }
            if (is_get) {
                found = deus_value_record_get(&stack[sp - 1u].managed, arg, arg_length);
            } else {
                found = deus_value_list_at(&stack[sp - 1u].managed, in.operand);
            }
            if (!found && !is_optional) { rc = fail(is_get ? "record field was not found" : "list index is out of bounds"); break; }
            if (!found) {
                value_dispose(&stack[sp - 1u]); stack[sp - 1u] = (Value){V_NULL, NULL, 0u, NULL, 0}; continue;
            }
            if (!value_from_managed(found, &extracted)) { rc = fail("structured value cannot be loaded"); break; }
            value_dispose(&stack[sp - 1u]); stack[sp - 1u] = extracted;
        } else if (in.opcode >= DEUS_EQUAL && in.opcode <= DEUS_COALESCE) {
            Value left, right; int result = 0;
            if (in.opcode == DEUS_BOOL_NOT) {
                if (!sp || stack[sp - 1u].kind != V_BOOL) { rc = fail("not requires Bool"); break; }
                stack[sp - 1u].scalar = !stack[sp - 1u].scalar; continue;
            }
            if (sp < 2u) { rc = fail("binary expression requires two values"); break; }
            right = stack[--sp]; left = stack[--sp];
            if (in.opcode == DEUS_COALESCE) {
                if (left.kind == V_NULL) { value_dispose(&left); stack[sp++] = right; }
                else { value_dispose(&right); stack[sp++] = left; }
                continue;
            }
            if (in.opcode == DEUS_BOOL_AND || in.opcode == DEUS_BOOL_OR) {
                if (left.kind != V_BOOL || right.kind != V_BOOL) { value_dispose(&left); value_dispose(&right); rc = fail("boolean operands required"); break; }
                result = in.opcode == DEUS_BOOL_AND ? (left.scalar && right.scalar) : (left.scalar || right.scalar);
            } else if (in.opcode >= DEUS_LESS && in.opcode <= DEUS_GREATER_EQUAL) {
                if (left.kind != V_I64 || right.kind != V_I64) { value_dispose(&left); value_dispose(&right); rc = fail("ordering requires I64"); break; }
                result = in.opcode == DEUS_LESS ? left.scalar < right.scalar : in.opcode == DEUS_LESS_EQUAL ? left.scalar <= right.scalar :
                         in.opcode == DEUS_GREATER ? left.scalar > right.scalar : left.scalar >= right.scalar;
            } else {
                int equal = left.kind == right.kind;
                if (equal && (left.kind == V_BOOL || left.kind == V_I64)) equal = left.scalar == right.scalar;
                else if (equal && (left.kind == V_STRING || left.kind == V_TEXT)) equal = left.len == right.len && !memcmp(left.data, right.data, left.len);
                else if (equal && left.kind != V_NULL) equal = 0;
                result = in.opcode == DEUS_EQUAL ? equal : !equal;
            }
            value_dispose(&left); value_dispose(&right); stack[sp++] = (Value){V_BOOL, NULL, 0u, NULL, result};
        } else if (in.opcode == DEUS_TO_TEXT || in.opcode == DEUS_TO_I64 || in.opcode == DEUS_TO_BOOL) {
            Value *value; char buffer[32]; char *end = NULL; long long number;
            if (!sp) { rc = fail("conversion requires a value"); break; } value = &stack[sp - 1u];
            if (in.opcode == DEUS_TO_TEXT) {
                if (value->kind == V_STRING || value->kind == V_TEXT) continue;
                int length = value->kind == V_I64 ? snprintf(buffer, sizeof(buffer), "%lld", (long long)value->scalar) :
                             value->kind == V_BOOL ? snprintf(buffer, sizeof(buffer), "%s", value->scalar ? "true" : "false") : -1;
                if (length < 0) { rc = fail("text conversion requires scalar"); break; }
                char *copy = (char *)malloc((size_t)length + 1u); if (!copy) { rc = fail("conversion allocation failed"); break; }
                memcpy(copy, buffer, (size_t)length + 1u); value_dispose(value); *value = (Value){V_STRING, copy, (size_t)length, NULL, 0};
            } else if (in.opcode == DEUS_TO_I64) {
                if (value->kind == V_I64) continue;
                if (value->kind == V_BOOL) { value->kind = V_I64; continue; }
                if (value->kind != V_STRING && value->kind != V_TEXT) { rc = fail("i64 conversion requires scalar"); break; }
                errno = 0; number = _strtoi64(value->data, &end, 10);
                if (errno == ERANGE || !end || (size_t)(end - value->data) != value->len) { rc = fail("invalid I64 text"); break; }
                value_dispose(value); *value = (Value){V_I64, NULL, 0u, NULL, number};
            } else {
                if (value->kind == V_BOOL) continue;
                if (value->kind == V_I64) { value->kind = V_BOOL; value->scalar = value->scalar != 0; continue; }
                if (value->kind != V_STRING && value->kind != V_TEXT) { rc = fail("bool conversion requires scalar"); break; }
                int boolean = value->len == 4u && !memcmp(value->data, "true", 4u) ? 1 : value->len == 5u && !memcmp(value->data, "false", 5u) ? 0 : -1;
                if (boolean < 0) { rc = fail("invalid Bool text"); break; }
                value_dispose(value); *value = (Value){V_BOOL, NULL, 0u, NULL, boolean};
            }
        } else if (in.opcode == DEUS_URL_ENCODE) {
            if (!began || !sp || !value_url_encode(&stack[sp - 1u])) { rc = fail("URL_ENCODE expected a bounded scalar"); break; }
        } else if (in.opcode == DEUS_URL_JOIN) {
            char *joined; Value *left, *right;
            if (!began || sp < 2u) { rc = fail("URL_JOIN requires two strings"); break; }
            left = &stack[sp - 2u]; right = &stack[sp - 1u];
            if ((left->kind != V_STRING && left->kind != V_TEXT) ||
                (right->kind != V_STRING && right->kind != V_TEXT) ||
                left->len > URL_MAX - right->len) { rc = fail("URL_JOIN exceeds URL limit or received non-string"); break; }
            joined = (char *)malloc(left->len + right->len + 1u);
            if (!joined) { rc = fail("URL_JOIN allocation failed"); break; }
            if (left->len) memcpy(joined, left->data, left->len);
            if (right->len) memcpy(joined + left->len, right->data, right->len);
            joined[left->len + right->len] = '\0'; size_t joined_length = left->len + right->len;
            value_dispose(right); value_dispose(left); sp -= 2u;
            stack[sp++] = (Value){V_STRING, joined, joined_length, NULL, 0};
        } else if (in.opcode == DEUS_HUNT_VALUE) {
            Value url; size_t length; char *body;
            if (!began || !sp || (stack[sp - 1u].kind != V_STRING && stack[sp - 1u].kind != V_TEXT)) {
                rc = fail("HUNT_VALUE expected URL string"); break;
            }
            url = stack[--sp]; body = runtime_hunt(&rt, url.data, &length, error, sizeof(error)); value_dispose(&url);
            if (!body) { rc = fail(error); break; }
            stack[sp++] = (Value){V_DOCUMENT, body, length, NULL, 0};
        } else if (in.opcode == DEUS_BIND) {
        } else if (in.opcode == DEUS_HOST_CALL) {
            if (!began || !sp || in.operand >= DEUS_MAX_LOCALS || local_bound[in.operand]) { rc = fail("invalid VM state at BIND"); break; }
            locals[in.operand] = stack[--sp]; memset(&stack[sp], 0, sizeof(stack[sp])); local_bound[in.operand] = 1u;
        } else if (in.opcode == DEUS_LOAD) {
            if (!began || sp == STACK_MAX || in.operand >= DEUS_MAX_LOCALS || !local_bound[in.operand] ||
                !value_clone(&locals[in.operand], &stack[sp])) { rc = fail("invalid VM state at LOAD"); break; }
            sp++;
        } else if (in.opcode == DEUS_AWAIT) {
            if (!sp || stack[sp - 1].kind != V_FUTURE) { rc = fail("AWAIT expected future"); break; }
            Value resolved;
            if (!task_resolve(stack[sp - 1].future, &resolved, error, sizeof(error))) { stack[sp - 1].future = NULL; rc = fail(error); break; }
            stack[sp - 1] = resolved;
        } else if (in.opcode == DEUS_JOIN) {
            if (!in.operand || in.operand > sp) { rc = fail("JOIN exceeds stack"); break; }
            size_t begin = sp - in.operand;
            for (size_t i = begin; i < sp; i++) if (stack[i].kind != V_FUTURE) { rc = fail("JOIN expected contiguous futures"); break; }
            if (rc) break;
            for (size_t i = begin; i < sp; i++) {
                Value resolved;
                if (!task_resolve(stack[i].future, &resolved, error, sizeof(error))) { stack[i].future = NULL; rc = fail(error); break; }
                stack[i] = resolved;
            }
            if (rc) break;
        } else if (in.opcode == DEUS_REAP) {
            if (!sp || stack[sp - 1].kind != V_DOCUMENT) { rc = fail("REAP expected document"); break; }
            Value old = stack[--sp]; size_t len; char *text = reap(old.data, old.len, arg, &len); free(old.data);
            if (!text) { rc = fail("REAP allocation failed"); break; }
            stack[sp++] = (Value){V_TEXT, text, len, NULL};
        } else if (in.opcode == DEUS_JSON_PATH) {
            DeusJsonScalar scalar; Value old;
            if (!sp || stack[sp - 1u].kind != V_DOCUMENT || in.operand >= p.string_count) {
                rc = fail("JSON_PATH expected document and path"); break;
            }
            old = stack[--sp];
            if (!deus_json_extract_scalar(old.data, old.len, arg, arg_length, &scalar, error, sizeof(error))) {
                value_dispose(&old); rc = fail(error); break;
            }
            value_dispose(&old);
            if (scalar.kind == DEUS_JSON_STRING) {
                stack[sp++] = (Value){V_STRING, scalar.string, scalar.string_length, NULL, 0};
                scalar.string = NULL;
            } else if (scalar.kind == DEUS_JSON_I64)
                stack[sp++] = (Value){V_I64, NULL, 0u, NULL, scalar.integer};
            else if (scalar.kind == DEUS_JSON_BOOL)
                stack[sp++] = (Value){V_BOOL, NULL, 0u, NULL, scalar.boolean ? 1 : 0};
            else stack[sp++] = (Value){V_NULL, NULL, 0u, NULL, 0};
            deus_json_scalar_dispose(&scalar);
        } else if (in.opcode == DEUS_EMIT || in.opcode == DEUS_DEBUG) {
            Value value; FILE *stream = in.opcode == DEUS_EMIT ? output : stderr;
            if (!sp) { rc = fail(in.opcode == DEUS_EMIT ? "EMIT expected a value" : "DEBUG expected a value"); break; }
            value = stack[--sp];
            if (value.kind == V_TEXT || value.kind == V_STRING) fwrite(value.data, 1, value.len, stream);
            else if (value.kind == V_NULL) fputs("null", stream);
            else if (value.kind == V_BOOL) fputs(value.scalar ? "true" : "false", stream);
            else if (value.kind == V_I64) fprintf(stream, "%lld", (long long)value.scalar);
            else if (value.kind == V_MANAGED) {
                if (!deus_value_write_json(&value.managed, stream)) { value_dispose(&value); rc = fail(in.opcode == DEUS_EMIT ? "EMIT failed to serialize compound value" : "DEBUG failed to serialize compound value"); break; }
            }
            else { value_dispose(&value); rc = fail(in.opcode == DEUS_EMIT ? "EMIT cannot serialize this value" : "DEBUG cannot serialize this value"); break; }
            value_dispose(&value);
        } else if (in.opcode == DEUS_HALT) break;
    }
    while (sp) value_dispose(&stack[--sp]);
    for (uint32_t slot = 0; slot < DEUS_MAX_LOCALS; slot++) if (local_bound[slot]) value_dispose(&locals[slot]);
    if (rt.slots) CloseHandle(rt.slots);
    if (rt.session) WinHttpCloseHandle(rt.session);
    DeleteCriticalSection(&rt.rate_lock);
    deus_value_context_destroy(value_context);
    return rc;
}

int deus_vm_execute_program(const DeusProgram *program, FILE *output) {
    return deus_vm_execute_program_with_host(program, output, NULL);
}

#ifndef DEUS_VM_NO_MAIN
int main(int argc, char **argv) {
    if (argc != 2) { fprintf(stderr, "usage: deusvm <file.deusb>\n"); return 64; }
    DeusProgram program; char error[192];
    if (!deus_read_binary(argv[1], &program, error, sizeof(error))) return fail(error);
    int rc = deus_vm_execute_program(&program, stdout);
    deus_program_free(&program);
    return rc;
}
#endif
