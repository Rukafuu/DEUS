#ifdef _WIN32
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "deus_project.h"

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

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__, __LINE__, #condition); abort(); \
} } while (0)

static void write_file(const char *path, const char *text) {
    FILE *file = fopen(path, "wb"); CHECK(file);
    CHECK(fwrite(text, 1u, strlen(text), file) == strlen(text)); CHECK(!fclose(file));
}

static char *read_file(const char *path) {
    FILE *file = fopen(path, "rb"); long size; char *text; CHECK(file);
    CHECK(!fseek(file, 0, SEEK_END)); size = ftell(file); CHECK(size >= 0); rewind(file);
    text = (char *)malloc((size_t)size + 1u); CHECK(text);
    CHECK(fread(text, 1u, (size_t)size, file) == (size_t)size); text[size] = '\0'; fclose(file); return text;
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
    CHECK(deus_project_resolve_input(root, &project, error, sizeof(error)));
    CHECK(!strcmp(project.name, "app")); CHECK(project.network_declared == 1);
    CHECK(project.dependency_count == 1u); CHECK(!strcmp(project.dependencies[0].version, "1.2.3"));
    CHECK(deus_project_write_lock(&project, error, sizeof(error)));
    first = read_file("deus_project_test_tmp\\deus.lock");
    CHECK(strstr(first, "name = \"helper\"")); CHECK(strstr(first, "path = \"dep\""));
    CHECK(deus_project_write_lock(&project, error, sizeof(error)));
    second = read_file("deus_project_test_tmp\\deus.lock"); CHECK(!strcmp(first, second));
    free(first); free(second);
    puts("project manifest and lockfile tests passed"); return 0;
}
