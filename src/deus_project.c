#include "deus_project.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <windows.h>
#define DEUS_SEP '\\'
#else
#include <unistd.h>
#define DEUS_SEP '/'
#endif

static void set_error(char *error, size_t size, const char *format, ...) {
    va_list args;
    if (!error || !size) return;
    va_start(args, format);
    (void)vsnprintf(error, size, format, args);
    va_end(args);
}

static int copy_text(char *target, size_t capacity, const char *source) {
    size_t length = strlen(source);
    if (length >= capacity) return 0;
    memcpy(target, source, length + 1u);
    return 1;
}

static int join_path(char *target, size_t capacity, const char *left, const char *right) {
    size_t left_length = strlen(left), right_length = strlen(right);
    int needs_separator = left_length && left[left_length - 1u] != '/' && left[left_length - 1u] != '\\';
    if (left_length + (size_t)needs_separator + right_length >= capacity) return 0;
    memcpy(target, left, left_length);
    if (needs_separator) target[left_length++] = DEUS_SEP;
    memcpy(target + left_length, right, right_length + 1u);
    return 1;
}

static int is_directory(const char *path) {
    struct stat info;
    return stat(path, &info) == 0 && (info.st_mode & S_IFDIR) != 0;
}

static int file_exists(const char *path) {
    struct stat info;
    return stat(path, &info) == 0 && (info.st_mode & S_IFREG) != 0;
}

static void parent_path(char *path) {
    char *slash = strrchr(path, '/'), *backslash = strrchr(path, '\\'), *last;
    last = slash > backslash ? slash : backslash;
    if (!last) { (void)copy_text(path, DEUS_PROJECT_PATH_MAX, "."); return; }
    if (last == path) last[1] = '\0'; else *last = '\0';
}

static char *trim(char *text) {
    char *end;
    while (isspace((unsigned char)*text)) text++;
    end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1])) end--;
    *end = '\0';
    return text;
}

static int parse_string(const char *value, char *target, size_t capacity) {
    size_t length;
    if (*value != '"') return 0;
    value++; length = strlen(value);
    if (!length || value[length - 1u] != '"') return 0;
    length--;
    if (length >= capacity || memchr(value, '"', length) || memchr(value, '\n', length)) return 0;
    memcpy(target, value, length); target[length] = '\0'; return 1;
}

static int valid_name(const char *name) {
    if (!name[0] || !(isalnum((unsigned char)name[0]) || name[0] == '_' || name[0] == '-')) return 0;
    for (name++; *name; name++)
        if (!(isalnum((unsigned char)*name) || *name == '_' || *name == '-')) return 0;
    return 1;
}

static int dependency_compare(const void *left, const void *right) {
    const DeusProjectDependency *a = (const DeusProjectDependency *)left;
    const DeusProjectDependency *b = (const DeusProjectDependency *)right;
    return strcmp(a->name, b->name);
}

static int parse_path_dependency(const char *value, char *path, size_t capacity) {
    char buffer[DEUS_PROJECT_PATH_MAX + 32u], *inside, *equal, *end;
    if (!copy_text(buffer, sizeof(buffer), value)) return 0;
    inside = trim(buffer);
    if (*inside != '{') return 0;
    end = strrchr(inside, '}');
    if (!end || trim(end + 1u)[0]) return 0;
    *end = '\0'; inside = trim(inside + 1u);
    equal = strchr(inside, '=');
    if (!equal) return 0;
    *equal = '\0';
    if (strcmp(trim(inside), "path")) return 0;
    return parse_string(trim(equal + 1u), path, capacity);
}

static int read_manifest(const char *manifest_path, DeusProject *project,
                         int dependencies, char *error, size_t error_size) {
    FILE *file = fopen(manifest_path, "rb");
    char line[2048], section[64] = "";
    unsigned line_number = 0u;
    int have_name = 0, have_version = 0, have_entry = 0;
    if (!file) { set_error(error, error_size, "cannot read manifest %s", manifest_path); return 0; }
    while (fgets(line, sizeof(line), file)) {
        char *text, *equal, *comment; line_number++;
        if (!strchr(line, '\n') && !feof(file)) {
            set_error(error, error_size, "%s:%u: line is too long", manifest_path, line_number); fclose(file); return 0;
        }
        text = trim(line); comment = strchr(text, '#');
        if (comment) { *comment = '\0'; text = trim(text); }
        if (!text[0]) continue;
        if (text[0] == '[') {
            size_t length = strlen(text);
            if (length < 3u || text[length - 1u] != ']' || length - 2u >= sizeof(section)) goto invalid;
            memcpy(section, text + 1u, length - 2u); section[length - 2u] = '\0'; continue;
        }
        equal = strchr(text, '=');
        if (!equal) goto invalid;
        *equal = '\0';
        {
            char *key = trim(text), *value = trim(equal + 1u);
            if (!strcmp(section, "package")) {
                if (!strcmp(key, "name")) have_name = parse_string(value, project->name, sizeof(project->name));
                else if (!strcmp(key, "version")) have_version = parse_string(value, project->version, sizeof(project->version));
                else if (!strcmp(key, "entry")) {
                    char relative[DEUS_PROJECT_PATH_MAX]; have_entry = parse_string(value, relative, sizeof(relative));
                    if (have_entry && !join_path(project->entry_path, sizeof(project->entry_path), project->root, relative)) have_entry = 0;
                }
            } else if (!strcmp(section, "capabilities") && !strcmp(key, "network")) {
                if (!strcmp(value, "true")) project->network_declared = 1;
                else if (!strcmp(value, "false")) project->network_declared = 0;
                else goto invalid;
            } else if (dependencies && !strcmp(section, "dependencies")) {
                DeusProjectDependency *dependency;
                if (project->dependency_count >= DEUS_PROJECT_MAX_DEPENDENCIES || !valid_name(key)) goto invalid;
                dependency = &project->dependencies[project->dependency_count];
                if (!copy_text(dependency->name, sizeof(dependency->name), key) ||
                    !parse_path_dependency(value, dependency->path, sizeof(dependency->path))) goto invalid;
                project->dependency_count++;
            }
        }
        continue;
invalid:
        set_error(error, error_size, "%s:%u: unsupported or invalid manifest syntax", manifest_path, line_number);
        fclose(file); return 0;
    }
    if (ferror(file)) { set_error(error, error_size, "cannot read manifest %s", manifest_path); fclose(file); return 0; }
    fclose(file);
    if (!have_name || !valid_name(project->name)) { set_error(error, error_size, "%s: missing or invalid package.name", manifest_path); return 0; }
    if (!have_version || !project->version[0]) { set_error(error, error_size, "%s: missing package.version", manifest_path); return 0; }
    if (!have_entry) { set_error(error, error_size, "%s: missing or invalid package.entry", manifest_path); return 0; }
    return 1;
}

static int load_dependency_metadata(DeusProject *project, char *error, size_t error_size) {
    size_t index;
    for (index = 0u; index < project->dependency_count; index++) {
        DeusProjectDependency *dependency = &project->dependencies[index];
        DeusProject metadata; char root[DEUS_PROJECT_PATH_MAX], manifest[DEUS_PROJECT_PATH_MAX];
        memset(&metadata, 0, sizeof(metadata));
        if (!join_path(root, sizeof(root), project->root, dependency->path) ||
            !join_path(manifest, sizeof(manifest), root, "deus.toml")) {
            set_error(error, error_size, "dependency path for %s is too long", dependency->name); return 0;
        }
        if (!copy_text(metadata.root, sizeof(metadata.root), root) || !read_manifest(manifest, &metadata, 0, error, error_size)) return 0;
        if (strcmp(metadata.name, dependency->name)) {
            set_error(error, error_size, "dependency %s declares package name %s", dependency->name, metadata.name); return 0;
        }
        if (!file_exists(metadata.entry_path)) {
            set_error(error, error_size, "dependency entry does not exist: %s", metadata.entry_path); return 0;
        }
        if (!copy_text(dependency->version, sizeof(dependency->version), metadata.version)) return 0;
    }
    qsort(project->dependencies, project->dependency_count, sizeof(project->dependencies[0]), dependency_compare);
    for (index = 1u; index < project->dependency_count; index++) {
        if (!strcmp(project->dependencies[index - 1u].name, project->dependencies[index].name)) {
            set_error(error, error_size, "duplicate dependency %s", project->dependencies[index].name); return 0;
        }
    }
    return 1;
}

int deus_project_resolve_input(const char *input, DeusProject *project,
                               char *error, size_t error_size) {
    size_t length;
    if (!input || !project) { set_error(error, error_size, "invalid project input"); return 0; }
    memset(project, 0, sizeof(*project)); length = strlen(input);
    if (length >= 5u && !strcmp(input + length - 5u, ".deus")) {
        if (!file_exists(input) || !copy_text(project->entry_path, sizeof(project->entry_path), input)) {
            set_error(error, error_size, "cannot read source %s", input); return 0;
        }
        return 1;
    }
    if (is_directory(input)) {
        if (!copy_text(project->root, sizeof(project->root), input) ||
            !join_path(project->manifest_path, sizeof(project->manifest_path), input, "deus.toml")) {
            set_error(error, error_size, "project path is too long"); return 0;
        }
    } else {
        if (!file_exists(input) || length < 9u || strcmp(input + length - 9u, "deus.toml") ||
            !copy_text(project->manifest_path, sizeof(project->manifest_path), input) ||
            !copy_text(project->root, sizeof(project->root), input)) {
            set_error(error, error_size, "expected a .deus file, deus.toml, or project directory: %s", input); return 0;
        }
        parent_path(project->root);
    }
    if (!read_manifest(project->manifest_path, project, 1, error, error_size)) return 0;
    if (!file_exists(project->entry_path)) { set_error(error, error_size, "project entry does not exist: %s", project->entry_path); return 0; }
    return load_dependency_metadata(project, error, error_size);
}

static void portable_path(FILE *file, const char *path) {
    for (; *path; path++) fputc(*path == '\\' ? '/' : *path, file);
}

int deus_project_write_lock(const DeusProject *project, char *error, size_t error_size) {
    char path[DEUS_PROJECT_PATH_MAX], temporary[DEUS_PROJECT_PATH_MAX + 16u];
    FILE *file; size_t index; int ok = 1, written;
    if (!project || !project->manifest_path[0]) return 1;
    written = join_path(path, sizeof(path), project->root, "deus.lock") ?
        snprintf(temporary, sizeof(temporary), "%s.tmp", path) : -1;
    if (written < 0 || (size_t)written >= sizeof(temporary)) {
        set_error(error, error_size, "lockfile path is too long"); return 0;
    }
    file = fopen(temporary, "wb");
    if (!file) { set_error(error, error_size, "cannot write %s", temporary); return 0; }
    if (fprintf(file, "# Generated by DEUS. Do not edit.\nlock-version = 1\n\n[package]\nname = \"%s\"\nversion = \"%s\"\n",
                project->name, project->version) < 0) ok = 0;
    for (index = 0u; ok && index < project->dependency_count; index++) {
        const DeusProjectDependency *dependency = &project->dependencies[index];
        if (fprintf(file, "\n[[dependency]]\nname = \"%s\"\nversion = \"%s\"\npath = \"", dependency->name, dependency->version) < 0) ok = 0;
        if (ok) portable_path(file, dependency->path);
        if (ok && fputs("\"\n", file) == EOF) ok = 0;
    }
    if (fclose(file)) ok = 0;
    if (!ok) { remove(temporary); set_error(error, error_size, "cannot write %s", path); return 0; }
#ifdef _WIN32
    if (!MoveFileExA(temporary, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
#else
    if (rename(temporary, path)) {
#endif
        remove(temporary); set_error(error, error_size, "cannot replace %s", path); return 0;
    }
    return 1;
}
