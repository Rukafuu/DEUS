#include "deus_project.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#define mkdir_one(path) _mkdir(path)
#else
#include <sys/stat.h>
#define mkdir_one(path) mkdir(path, 0700)
#endif

static void write_file(const char *path, const char *text) {
    FILE *file = fopen(path, "wb"); assert(file);
    assert(fwrite(text, 1u, strlen(text), file) == strlen(text)); assert(!fclose(file));
}

static char *read_file(const char *path) {
    FILE *file = fopen(path, "rb"); long size; char *text; assert(file);
    assert(!fseek(file, 0, SEEK_END)); size = ftell(file); assert(size >= 0); rewind(file);
    text = (char *)malloc((size_t)size + 1u); assert(text);
    assert(fread(text, 1u, (size_t)size, file) == (size_t)size); text[size] = '\0'; fclose(file); return text;
}

int main(void) {
    const char *root = "deus_project_test_tmp", *dep = "deus_project_test_tmp\\dep";
    DeusProject project; char error[512] = ""; char *first, *second;
    (void)remove("deus_project_test_tmp\\deus.lock");
    (void)remove("deus_project_test_tmp\\src\\main.deus");
    (void)remove("deus_project_test_tmp\\dep\\src\\main.deus");
    (void)remove("deus_project_test_tmp\\deus.toml");
    (void)remove("deus_project_test_tmp\\dep\\deus.toml");
    (void)mkdir_one(root); (void)mkdir_one("deus_project_test_tmp\\src");
    (void)mkdir_one(dep); (void)mkdir_one("deus_project_test_tmp\\dep\\src");
    write_file("deus_project_test_tmp\\src\\main.deus", "genesis halt\n");
    write_file("deus_project_test_tmp\\dep\\src\\main.deus", "genesis halt\n");
    write_file("deus_project_test_tmp\\dep\\deus.toml",
               "[package]\nname = \"helper\"\nversion = \"1.2.3\"\nentry = \"src/main.deus\"\n");
    write_file("deus_project_test_tmp\\deus.toml",
               "[package]\nname = \"app\"\nversion = \"0.1.0\"\nentry = \"src/main.deus\"\n\n"
               "[capabilities]\nnetwork = true\n\n[dependencies]\nhelper = { path = \"dep\" }\n");
    assert(deus_project_resolve_input(root, &project, error, sizeof(error)));
    assert(!strcmp(project.name, "app")); assert(project.network_declared == 1);
    assert(project.dependency_count == 1u); assert(!strcmp(project.dependencies[0].version, "1.2.3"));
    assert(deus_project_write_lock(&project, error, sizeof(error)));
    first = read_file("deus_project_test_tmp\\deus.lock");
    assert(strstr(first, "name = \"helper\"")); assert(strstr(first, "path = \"dep\""));
    assert(deus_project_write_lock(&project, error, sizeof(error)));
    second = read_file("deus_project_test_tmp\\deus.lock"); assert(!strcmp(first, second));
    free(first); free(second);
    puts("project manifest and lockfile tests passed"); return 0;
}
