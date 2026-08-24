#include "deus.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

typedef struct { SOCKET socket; } Client;
typedef struct { SOCKET listener; HANDLE clients[2]; } Server;
typedef struct { unsigned hunts, releases; } MockHostState;

static int mock_hunt(void *context, const char *url, size_t url_length,
                     DeusHostDocument *document, char *error, size_t error_cap) {
    static const char body[] = "{\"results\":[{\"title\":\"Host\",\"year\":2023,\"ok\":true}]}";
    MockHostState *state = (MockHostState *)context;
    (void)error; (void)error_cap;
    if (url_length != 35u || memcmp(url, "deus://catalog/frieren%20white/2023", 35u)) return 0;
    state->hunts++; document->data = body; document->length = sizeof(body) - 1u;
    document->status = 200u; document->token = NULL; return 1;
}

static void mock_release(void *context, DeusHostDocument *document) {
    MockHostState *state = (MockHostState *)context; state->releases++; memset(document, 0, sizeof(*document));
}

static int mock_html_hunt(void *context, const char *url, size_t url_length,
                          DeusHostDocument *document, char *error, size_t error_cap) {
    static const char body[] =
        "<main><h1 id=\"title\" class=\"hero featured\">Hello <span>nested <em>inline</em></span> HTML</h1></main>";
    MockHostState *state = (MockHostState *)context;
    (void)error; (void)error_cap;
    if (url_length != 11u || memcmp(url, "deus://html", 11u)) return 0;
    state->hunts++; document->data = body; document->length = sizeof(body) - 1u;
    document->status = 200u; document->token = NULL; return 1;
}

static DWORD WINAPI serve_client(LPVOID context) {
    Client *client = (Client *)context; char request[1024];
    recv(client->socket, request, sizeof(request), 0); Sleep(700);
    static const char response[] = "HTTP/1.1 200 OK\r\nContent-Length: 10\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n<h1>X</h1>";
    send(client->socket, response, (int)strlen(response), 0);
    closesocket(client->socket); free(client); return 0;
}

static DWORD WINAPI serve(LPVOID context) {
    Server *server = (Server *)context;
    for (int i = 0; i < 2; i++) {
        SOCKET accepted = accept(server->listener, NULL, NULL);
        if (accepted == INVALID_SOCKET) return 1;
        Client *client = (Client *)malloc(sizeof(*client));
        if (!client) { closesocket(accepted); return 1; }
        client->socket = accepted;
        server->clients[i] = CreateThread(NULL, 0, serve_client, client, 0, NULL);
        if (!server->clients[i]) { closesocket(accepted); free(client); return 1; }
    }
    WaitForMultipleObjects(2, server->clients, TRUE, INFINITE);
    CloseHandle(server->clients[0]); CloseHandle(server->clients[1]);
    closesocket(server->listener); return 0;
}

static int read_output(const char *path) {
    FILE *file = fopen(path, "rb"); if (!file) return 0;
    char output[32] = {0}; size_t n = fread(output, 1, sizeof(output) - 1, file);
    fclose(file); return n == 4 && !memcmp(output, "X\nX\n", 4);
}

static int test_locals(void) {
    DeusString strings[] = {{"frieren", 7u}};
    DeusInstruction code[] = {
        {DEUS_GENESIS, 0u}, {DEUS_CONST, 0u}, {DEUS_BIND, 0u},
        {DEUS_LOAD, 0u}, {DEUS_EMIT, 0u},
        {DEUS_CONST_I64, 0u, -42}, {DEUS_EMIT, 0u},
        {DEUS_CONST_BOOL, 1u}, {DEUS_EMIT, 0u},
        {DEUS_CONST_NULL, 0u}, {DEUS_EMIT, 0u}, {DEUS_HALT, 0u}
    };
    DeusProgram program = {strings, 1u, code, 12u};
    FILE *output = NULL; char text[32] = {0};
    if (fopen_s(&output, "deus_local_output.txt", "wb") || !output) return 0;
    int exit_code = deus_vm_execute_program(&program, output); fclose(output);
    output = NULL;
    if (fopen_s(&output, "deus_local_output.txt", "rb") || !output) return 0;
    size_t read = fread(text, 1, sizeof(text) - 1u, output); fclose(output); remove("deus_local_output.txt");
    return exit_code == 0 && read == 18u && !memcmp(text, "frieren-42truenull", 18u);
}

static int test_compounds(void) {
    const char *source = "genesis\nbind result = {\n\"title\": \"Frieren\",\n\"year\": 2023,\n\"meta\": {\"verified\": true}\n}\nbind results = [result]\nbind first = at results 0\nbind copied_title = get first \"title\"\nbind missing = get? first \"subtitle\"\nbind absent = at? results 9\nload copied_title\nemit\nload missing\nemit\nload absent\nemit\nload results\nemit\nhalt\n";
    const char *expected = "Frierennullnull[{\"title\":\"Frieren\",\"year\":2023,\"meta\":{\"verified\":true}}]";
    DeusProgram program; DeusDiagnostic diagnostic = {0}; FILE *output = NULL; char text[128] = {0};
    if (!deus_parse_source(source, strlen(source), &program, &diagnostic)) return 0;
    if (fopen_s(&output, "deus_compound_output.txt", "wb") || !output) { deus_program_free(&program); return 0; }
    int exit_code = deus_vm_execute_program(&program, output); fclose(output); output = NULL;
    if (fopen_s(&output, "deus_compound_output.txt", "rb") || !output) { deus_program_free(&program); return 0; }
    size_t read = fread(text, 1, sizeof(text) - 1u, output); fclose(output); remove("deus_compound_output.txt");
    deus_program_free(&program);
    return exit_code == 0 && read == strlen(expected) && !memcmp(text, expected, read);
}

static int test_expressions(void) {
    const char *source = "genesis\nbind score = 95\nbind minimum = 80\nbind verified = true\nbind eligible = verified and score >= minimum\nbind label = null ?? \"fallback\"\nbind score_text = text(score)\nbind parsed = i64(\"42\")\nbind truth = bool(1)\nbind unequal = score != minimum\nbind inverted = not false\nload eligible\nemit\nload label\nemit\nload score_text\nemit\nload parsed\nemit\nload truth\nemit\nload unequal\nemit\nload inverted\nemit\nhalt\n";
    const char *expected = "truefallback9542truetruetrue";
    DeusProgram program; DeusDiagnostic diagnostic = {0}; FILE *output = NULL; char text[64] = {0};
    if (!deus_parse_source(source, strlen(source), &program, &diagnostic)) {
        fprintf(stderr, "expression source %u:%u: %s\n", diagnostic.line, diagnostic.column, diagnostic.message); return 0;
    }
    if (fopen_s(&output, "deus_expression_output.txt", "wb") || !output) { deus_program_free(&program); return 0; }
    int exit_code = deus_vm_execute_program(&program, output); fclose(output); output = NULL;
    if (fopen_s(&output, "deus_expression_output.txt", "rb") || !output) { deus_program_free(&program); return 0; }
    size_t read = fread(text, 1, sizeof(text) - 1u, output); fclose(output); remove("deus_expression_output.txt");
    deus_program_free(&program); return exit_code == 0 && read == strlen(expected) && !memcmp(text, expected, read);
}

static int test_embedded_host(void) {
    const char *source = "omni \"net.http2\"\ngenesis\nbind query = \"frieren white\"\nbind year = 2023\nbind page = hunt \"deus://catalog/{query}/{year}\"\nbind title = json page \"$.results[0].title\"\nbind result_year = json page \"$.results[0].year\"\nbind ok = json page \"$.results[0].ok\"\nload title\nemit\nload result_year\nemit\nload ok\nemit\nhalt\n";
    DeusProgram program; DeusDiagnostic diagnostic = {0}; MockHostState state = {0};
    DeusHost host = {DEUS_HOST_ABI_VERSION, DEUS_HOST_CAP_NETWORK, &state, mock_hunt, mock_release};
    FILE *output = NULL; char text[32] = {0};
    if (!deus_parse_source(source, strlen(source), &program, &diagnostic)) {
        fprintf(stderr, "host source %u:%u: %s\n", diagnostic.line, diagnostic.column, diagnostic.message); return 0;
    }
    if (fopen_s(&output, "deus_host_output.txt", "wb") || !output) { deus_program_free(&program); return 0; }
    int exit_code = deus_vm_execute_program_with_host(&program, output, &host); fclose(output); output = NULL;
    if (fopen_s(&output, "deus_host_output.txt", "rb") || !output) { deus_program_free(&program); return 0; }
    size_t read = fread(text, 1, sizeof(text) - 1u, output); fclose(output); remove("deus_host_output.txt");
    int ok = exit_code == 0 && state.hunts == 1u && state.releases == 1u &&
             read == 12u && !memcmp(text, "Host2023true", 12u);
    deus_program_free(&program); return ok;
}

static int test_nested_reap(void) {
    const char *source =
        "omni \"net.http2\"\ngenesis\n"
        "bind page = hunt \"deus://html\"\n"
        "bind by_tag = reap page \"h1\"\nload by_tag\nemit\n"
        "bind by_class = reap page \".featured\"\nload by_class\nemit\n"
        "bind by_id = reap page \"#title\"\nload by_id\nemit\nhalt\n";
    const char *line = "Hello nested inline HTML\n";
    DeusProgram program; DeusDiagnostic diagnostic = {0}; MockHostState state = {0};
    DeusHost host = {DEUS_HOST_ABI_VERSION, DEUS_HOST_CAP_NETWORK, &state, mock_html_hunt, mock_release};
    FILE *output = NULL; char text[96] = {0};
    if (!deus_parse_source(source, strlen(source), &program, &diagnostic)) return 0;
    if (fopen_s(&output, "deus_reap_output.txt", "wb") || !output) { deus_program_free(&program); return 0; }
    int exit_code = deus_vm_execute_program_with_host(&program, output, &host); fclose(output); output = NULL;
    if (fopen_s(&output, "deus_reap_output.txt", "rb") || !output) { deus_program_free(&program); return 0; }
    size_t read = fread(text, 1, sizeof(text) - 1u, output); fclose(output); remove("deus_reap_output.txt");
    size_t line_length = strlen(line); int ok = exit_code == 0 && state.hunts == 1u && state.releases == 1u &&
        read == line_length * 3u && !memcmp(text, line, line_length) &&
        !memcmp(text + line_length, line, line_length) && !memcmp(text + line_length * 2u, line, line_length);
    deus_program_free(&program); return ok;
}

static int test_operational_diagnostics(void) {
    const char *wrong_type = "genesis\nbind value = 42\nbind title = reap value \"h1\"\nhalt\n";
    const char *late_config = "genesis\nhunt \"https://example.test\"\nlimit 2\nreap \"h1\"\nemit\nhalt\n";
    const char *unknown_placeholder = "genesis\nbind page = hunt \"https://example.test/{missing}\"\nhalt\n";
    const char *bad_placeholder = "genesis\nbind query = \"x\"\nbind page = hunt \"https://example.test/{query\"\nhalt\n";
    DeusProgram program; DeusDiagnostic diagnostic = {0};
    if (deus_parse_source(wrong_type, strlen(wrong_type), &program, &diagnostic) ||
        !strstr(diagnostic.message, "Document")) return 0;
    memset(&diagnostic, 0, sizeof(diagnostic));
    if (deus_parse_source(late_config, strlen(late_config), &program, &diagnostic) ||
        !strstr(diagnostic.message, "network execution")) return 0;
    memset(&diagnostic, 0, sizeof(diagnostic));
    if (deus_parse_source(unknown_placeholder, strlen(unknown_placeholder), &program, &diagnostic) ||
        !strstr(diagnostic.message, "unknown local")) return 0;
    memset(&diagnostic, 0, sizeof(diagnostic));
    return !deus_parse_source(bad_placeholder, strlen(bad_placeholder), &program, &diagnostic) &&
           strstr(diagnostic.message, "unterminated placeholder") != NULL;
}

int main(void) {
    if (!test_locals()) { fprintf(stderr, "local VM test failed\n"); return 1; }
    if (!test_expressions()) { fprintf(stderr, "expression VM test failed\n"); return 1; }
    if (!test_compounds()) { fprintf(stderr, "compound VM test failed\n"); return 1; }
    if (!test_embedded_host()) { fprintf(stderr, "embedded host test failed\n"); return 1; }
    if (!test_nested_reap()) { fprintf(stderr, "nested reap test failed\n"); return 1; }
    if (!test_operational_diagnostics()) { fprintf(stderr, "operational diagnostic test failed\n"); return 1; }
    WSADATA wsa; if (WSAStartup(MAKEWORD(2, 2), &wsa)) return 1;
    Server server = {0}; server.listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    struct sockaddr_in address = {0}; address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (server.listener == INVALID_SOCKET || bind(server.listener, (SOCKADDR *)&address, sizeof(address)) || listen(server.listener, 2)) return 1;
    int address_len = sizeof(address);
    if (getsockname(server.listener, (SOCKADDR *)&address, &address_len)) return 1;
    unsigned port = ntohs(address.sin_port);
    HANDLE server_thread = CreateThread(NULL, 0, serve, &server, 0, NULL);
    if (!server_thread) return 1;

    char url_a[128], url_b[128];
    snprintf(url_a, sizeof(url_a), "http://127.0.0.1:%u/a", port);
    snprintf(url_b, sizeof(url_b), "http://127.0.0.1:%u/b", port);
    DeusString strings[] = {{"net.http2", 9}, {url_a, (uint32_t)strlen(url_a)},
                            {url_b, (uint32_t)strlen(url_b)}, {"h1", 2}};
    DeusInstruction code[] = {
        {DEUS_OMNI, 0}, {DEUS_GENESIS, 0}, {DEUS_LIMIT, 2}, {DEUS_RETRY, 0},
        {DEUS_FORK, 1}, {DEUS_FORK, 2}, {DEUS_JOIN, 2},
        {DEUS_REAP, 3}, {DEUS_EMIT, 0}, {DEUS_REAP, 3}, {DEUS_EMIT, 0}, {DEUS_HALT, 0}
    };
    DeusProgram program = {strings, 4, code, 12}; char error[192];
    if (!deus_write_binary(&program, "deus_async_test.deusb", error, sizeof(error))) { fprintf(stderr, "%s\n", error); return 1; }
    FILE *output = fopen("deus_async_output.txt", "wb");
    if (!output) return 1;
    ULONGLONG begin = GetTickCount64();
    int exit_code = deus_vm_execute_program(&program, output);
    ULONGLONG elapsed = GetTickCount64() - begin;
    fclose(output);
    WaitForSingleObject(server_thread, INFINITE); CloseHandle(server_thread); WSACleanup();
    int output_ok = read_output("deus_async_output.txt");
    remove("deus_async_test.deusb"); remove("deus_async_output.txt");
    if (exit_code || !output_ok || elapsed >= 1200) {
        fprintf(stderr, "exit=%d output=%d elapsed=%llu ms\n", exit_code, output_ok, (unsigned long long)elapsed); return 1;
    }
    printf("two delayed hunts joined in %llu ms\n", (unsigned long long)elapsed); return 0;
}
