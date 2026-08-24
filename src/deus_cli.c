#include "deus.h"
#include "deus_project.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#endif

#define DEUS_VERSION "0.1.0"

static char *load_source(const char *path, size_t *length);
static void show_diagnostic(const char *path, const char *source, size_t length,
                            const DeusDiagnostic *diagnostic);

static void usage(FILE *output) {
    fprintf(output,
        "DEUS %s - search and crawling language\n\n"
        "usage:\n"
        "  deus run <file.deus|project>\n"
        "  deus check <file.deus|project>\n"
        "  deus build <file.deus|project> [-o file.deusb]\n"
        "  deus exec <file.deusb>\n"
        "  deus fmt [--check] <file.deus>\n"
        "  deus init <directory> [--template minimal|crawler|ranking]\n"
        "  deus version\n",
        DEUS_VERSION);
}

static int write_text(const char *path, const char *text) {
    FILE *file = fopen(path, "wb"); size_t length = strlen(text); int ok;
    if (!file) return 0;
    ok = fwrite(text, 1u, length, file) == length;
    if (fclose(file)) ok = 0;
    return ok;
}

static int replace_text(const char *path, const char *text) {
    size_t length = strlen(path); char *temporary = (char *)malloc(length + 14u); int ok;
    if (!temporary) return 0;
    memcpy(temporary, path, length); memcpy(temporary + length, ".deusfmt.tmp", 13u);
    ok = write_text(temporary, text);
#ifdef _WIN32
    if (ok) ok = MoveFileExA(temporary, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    if (ok) ok = rename(temporary, path) == 0;
#endif
    if (!ok) remove(temporary);
    free(temporary); return ok;
}

static int command_fmt(const char *path, int check_only) {
    size_t length, output_length = 0u, capacity; char *source = load_source(path, &length);
    char *formatted; DeusProgram program; DeusDiagnostic diagnostic = {0}; unsigned depth = 0u;
    if (!source) { fprintf(stderr, "deus: cannot read %s\n", path); return 66; }
    if (!deus_parse_source(source, length, &program, &diagnostic)) {
        show_diagnostic(path, source, length, &diagnostic); free(source); return 65;
    }
    deus_program_free(&program);
    capacity = length * 3u + 3u; formatted = (char *)malloc(capacity);
    if (!formatted) { free(source); fprintf(stderr, "deus: out of memory\n"); return 74; }
    for (size_t at = 0u; at < length;) {
        size_t start = at, end, content, trim; int in_string = 0, escaped = 0, comment = 0;
        while (at < length && source[at] != '\n' && source[at] != '\r') at++;
        end = at; while (at < length && (source[at] == '\n' || source[at] == '\r')) at++;
        content = start; while (content < end && (source[content] == ' ' || source[content] == '\t')) content++;
        trim = end; while (trim > content && (source[trim - 1u] == ' ' || source[trim - 1u] == '\t')) trim--;
        if (content < trim && (source[content] == '}' || source[content] == ']') && depth) depth--;
        if (content < trim) {
            for (unsigned indent = 0u; indent < depth * 2u; indent++) formatted[output_length++] = ' ';
            memcpy(formatted + output_length, source + content, trim - content); output_length += trim - content;
        }
        formatted[output_length++] = '\n';
        for (size_t cursor = content; cursor < trim; cursor++) {
            char character = source[cursor];
            if (comment) break;
            if (in_string) {
                if (escaped) escaped = 0;
                else if (character == '\\') escaped = 1;
                else if (character == '"') in_string = 0;
            } else if (character == '"') in_string = 1;
            else if (character == '#' || (character == '/' && cursor + 1u < trim && source[cursor + 1u] == '/')) comment = 1;
            else if (character == '{' || character == '[') depth++;
            else if ((character == '}' || character == ']') && cursor != content && depth) depth--;
        }
    }
    formatted[output_length] = '\0';
    while (output_length > 1u && formatted[output_length - 1u] == '\n' && formatted[output_length - 2u] == '\n')
        formatted[--output_length] = '\0';
    if (!output_length || formatted[output_length - 1u] != '\n') { formatted[output_length++] = '\n'; formatted[output_length] = '\0'; }
    if (length == output_length && !memcmp(source, formatted, length)) {
        printf("%s: formatted\n", path); free(formatted); free(source); return 0;
    }
    if (check_only) { fprintf(stderr, "%s: needs formatting\n", path); free(formatted); free(source); return 1; }
    if (!replace_text(path, formatted)) { fprintf(stderr, "deus: cannot write %s\n", path); free(formatted); free(source); return 74; }
    printf("formatted %s\n", path); free(formatted); free(source); return 0;
}

static int valid_package_name(const char *name) {
    if (!name[0]) return 0;
    for (; *name; name++) if (!isalnum((unsigned char)*name) && *name != '-' && *name != '_') return 0;
    return 1;
}

static int command_init(const char *directory, const char *template_name) {
    const char *name = strrchr(directory, '\\'); char src[1024], manifest[1024], main_path[1024], ignore[1024];
    const char *program = "genesis\nbind message = \"Hello from DEUS\"\nload message\nemit\nhalt\n";
    if (!name) name = strrchr(directory, '/'); name = name ? name + 1 : directory;
    if (!valid_package_name(name)) { fprintf(stderr, "deus: project directory must end in a package name\n"); return 64; }
    if (strcmp(template_name, "minimal") && strcmp(template_name, "crawler") && strcmp(template_name, "ranking")) {
        fprintf(stderr, "deus: unknown template '%s'\n", template_name); return 64;
    }
    if (!strcmp(template_name, "crawler")) program =
        "omni \"net.http2\"\ngenesis\nlimit 4\nretry 2\n"
        "bind page = hunt \"https://example.com\"\nbind title = reap page \"h1\"\nload title\nemit\nhalt\n";
    else if (!strcmp(template_name, "ranking")) program =
        "genesis\nbind score = 95\nbind minimum = 80\n"
        "bind eligible = score >= minimum\nload eligible\nemit\nhalt\n";
#ifdef _WIN32
    if (_mkdir(directory)) { fprintf(stderr, "deus: cannot create %s (it may already exist)\n", directory); return 73; }
#else
    (void)directory; fprintf(stderr, "deus: init is currently supported on Windows\n"); return 69;
#endif
    if (snprintf(src, sizeof(src), "%s\\src", directory) < 0 || _mkdir(src) ||
        snprintf(manifest, sizeof(manifest), "%s\\deus.toml", directory) < 0 ||
        snprintf(main_path, sizeof(main_path), "%s\\src\\main.deus", directory) < 0 ||
        snprintf(ignore, sizeof(ignore), "%s\\.gitignore", directory) < 0) {
        fprintf(stderr, "deus: cannot create project layout\n"); return 73;
    }
    {
        char contents[2048]; int written = snprintf(contents, sizeof(contents),
            "[package]\nname = \"%s\"\nversion = \"0.1.0\"\nentry = \"src/main.deus\"\n\n"
            "[capabilities]\nnetwork = %s\n", name, !strcmp(template_name, "crawler") ? "true" : "false");
        if (written < 0 || (size_t)written >= sizeof(contents) || !write_text(manifest, contents) ||
            !write_text(main_path, program) || !write_text(ignore, "target/\n*.deusb\n")) {
            fprintf(stderr, "deus: cannot write project files\n"); return 74;
        }
    }
    printf("created %s (%s)\nnext: deus run %s\\src\\main.deus\n", directory, template_name, directory); return 0;
}

static char *load_source(const char *path, size_t *length) {
    FILE *file = fopen(path, "rb"); long size; char *source;
    if (!file || fseek(file, 0, SEEK_END) || (size = ftell(file)) < 0) {
        if (file) fclose(file); return NULL;
    }
    rewind(file); source = (char *)malloc((size_t)size + 1u);
    if (!source || (size && fread(source, 1u, (size_t)size, file) != (size_t)size)) {
        free(source); fclose(file); return NULL;
    }
    fclose(file); source[size] = '\0'; *length = (size_t)size; return source;
}

static void show_diagnostic(const char *path, const char *source, size_t length,
                            const DeusDiagnostic *diagnostic) {
    size_t start = 0u, end; unsigned line = 1u;
    fprintf(stderr, "%s:%u:%u: error: %s\n", path, diagnostic->line,
            diagnostic->column, diagnostic->message);
    while (start < length && line < diagnostic->line) if (source[start++] == '\n') line++;
    end = start; while (end < length && source[end] != '\n' && source[end] != '\r') end++;
    if (line != diagnostic->line) return;
    fprintf(stderr, "  %.*s\n  ", (int)(end - start), source + start);
    for (unsigned column = 1u; column < diagnostic->column; column++)
        fputc(start + column - 1u < end && source[start + column - 1u] == '\t' ? '\t' : ' ', stderr);
    fputs("^\n", stderr);
}

static int compile_source(const char *path, DeusProgram *program) {
    size_t length; char *source = load_source(path, &length); DeusDiagnostic diagnostic = {0};
    if (!source) { fprintf(stderr, "deus: cannot read %s\n", path); return 66; }
    if (!deus_parse_source(source, length, program, &diagnostic)) {
        show_diagnostic(path, source, length, &diagnostic); free(source); return 65;
    }
    free(source); return 0;
}

static char *default_output_path(const char *source_path) {
    size_t length = strlen(source_path), stem = length; char *output;
    if (length >= 5u && !strcmp(source_path + length - 5u, ".deus")) stem -= 5u;
    output = (char *)malloc(stem + 7u); if (!output) return NULL;
    memcpy(output, source_path, stem); memcpy(output + stem, ".deusb", 7u); return output;
}

static int command_source(const char *command, const char *path, const char *output_path) {
    DeusProgram program; int rc = compile_source(path, &program);
    if (rc) return rc;
    if (!strcmp(command, "check")) printf("%s: clean\n", path);
    else if (!strcmp(command, "run")) rc = deus_vm_execute_program(&program, stdout);
    else {
        char error[192]; char *generated = NULL; const char *target = output_path;
        if (!target) { generated = default_output_path(path); target = generated; }
        if (!target) { fprintf(stderr, "deus: out of memory\n"); rc = 74; }
        else if (!deus_write_binary(&program, target, error, sizeof(error))) {
            fprintf(stderr, "deus: %s\n", error); rc = 74;
        } else printf("forged %s (%u instructions, %u strings)\n",
                      target, program.code_count, program.string_count);
        free(generated);
    }
    deus_program_free(&program); return rc;
}

static int command_input(const char *command, const char *input, const char *output_path) {
    DeusProject project; char error[512], project_output[DEUS_PROJECT_PATH_MAX];
    if (!deus_project_resolve_input(input, &project, error, sizeof(error))) {
        fprintf(stderr, "deus: %s\n", error); return 65;
    }
    if (project.manifest_path[0] && !deus_project_write_lock(&project, error, sizeof(error))) {
        fprintf(stderr, "deus: %s\n", error); return 74;
    }
    if (!strcmp(command, "build") && project.manifest_path[0] && !output_path) {
        char target[DEUS_PROJECT_PATH_MAX]; int written;
        written = snprintf(target, sizeof(target), "%s\\target", project.root);
        if (written < 0 || (size_t)written >= sizeof(target) ||
            (_mkdir(target) && errno != EEXIST)) {
            fprintf(stderr, "deus: cannot create project target directory\n"); return 73;
        }
        written = snprintf(project_output, sizeof(project_output), "%s\\%s.deusb", target, project.name);
        if (written < 0 || (size_t)written >= sizeof(project_output)) {
            fprintf(stderr, "deus: project output path is too long\n"); return 74;
        }
        output_path = project_output;
    }
    return command_source(command, project.entry_path, output_path);
}

static int command_exec(const char *path) {
    DeusProgram program; char error[192]; int rc;
    if (!deus_read_binary(path, &program, error, sizeof(error))) {
        fprintf(stderr, "deus: %s\n", error); return 65;
    }
    rc = deus_vm_execute_program(&program, stdout); deus_program_free(&program); return rc;
}

int main(int argc, char **argv) {
    const char *command;
    if (argc < 2 || !strcmp(argv[1], "help") || !strcmp(argv[1], "--help") || !strcmp(argv[1], "-h")) {
        usage(argc < 2 ? stderr : stdout); return argc < 2 ? 64 : 0;
    }
    command = argv[1];
    if (!strcmp(command, "version") || !strcmp(command, "--version")) {
        printf("DEUS %s (bytecode ABI %u, host ABI %u)\n", DEUS_VERSION,
               DEUS_ABI_VERSION, DEUS_HOST_ABI_VERSION); return 0;
    }
    if (!strcmp(command, "exec")) {
        if (argc != 3) { usage(stderr); return 64; }
        return command_exec(argv[2]);
    }
    if (!strcmp(command, "fmt")) {
        int check_only = argc == 4 && !strcmp(argv[2], "--check");
        if ((!check_only && argc != 3) || (check_only && argc != 4)) { usage(stderr); return 64; }
        return command_fmt(argv[check_only ? 3 : 2], check_only);
    }
    if (!strcmp(command, "init")) {
        const char *template_name = "minimal";
        if (argc == 5 && !strcmp(argv[3], "--template")) template_name = argv[4];
        else if (argc != 3) { usage(stderr); return 64; }
        return command_init(argv[2], template_name);
    }
    if (!strcmp(command, "check") || !strcmp(command, "run")) {
        if (argc != 3) { usage(stderr); return 64; }
        return command_input(command, argv[2], NULL);
    }
    if (!strcmp(command, "build")) {
        const char *output = NULL;
        if (argc == 5 && !strcmp(argv[3], "-o")) output = argv[4];
        else if (argc != 3) { usage(stderr); return 64; }
        return command_input(command, argv[2], output);
    }
    fprintf(stderr, "deus: unknown command '%s'\n", command); usage(stderr); return 64;
}
