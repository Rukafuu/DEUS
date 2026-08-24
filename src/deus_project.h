#ifndef DEUS_PROJECT_H
#define DEUS_PROJECT_H

#include <stddef.h>

#define DEUS_PROJECT_PATH_MAX 1024
#define DEUS_PROJECT_NAME_MAX 128
#define DEUS_PROJECT_VERSION_MAX 64
#define DEUS_PROJECT_MAX_DEPENDENCIES 64

typedef struct {
    char name[DEUS_PROJECT_NAME_MAX];
    char path[DEUS_PROJECT_PATH_MAX];
    char version[DEUS_PROJECT_VERSION_MAX];
} DeusProjectDependency;

typedef struct {
    char root[DEUS_PROJECT_PATH_MAX];
    char manifest_path[DEUS_PROJECT_PATH_MAX];
    char entry_path[DEUS_PROJECT_PATH_MAX];
    char name[DEUS_PROJECT_NAME_MAX];
    char version[DEUS_PROJECT_VERSION_MAX];
    int network_declared;
    DeusProjectDependency dependencies[DEUS_PROJECT_MAX_DEPENDENCIES];
    size_t dependency_count;
} DeusProject;

/* Accepts a .deus file, a deus.toml file, or a project directory. */
int deus_project_resolve_input(const char *input, DeusProject *project,
                               char *error, size_t error_size);

/* Writes a deterministic lockfile for validated local/path dependencies. */
int deus_project_write_lock(const DeusProject *project, char *error, size_t error_size);

#endif
