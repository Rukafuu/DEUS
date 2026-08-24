#include "deus.h"

#include <stdio.h>

int main(int argc, char **argv) {
    DeusProgram program;
    char error[192];
    int result;
    if (argc != 2) {
        fputs("usage: deusvm <file.deusb>\n", stderr);
        return 64;
    }
    if (!deus_read_binary(argv[1], &program, error, sizeof(error))) {
        fprintf(stderr, "deusvm: %s\n", error);
        return 1;
    }
    result = deus_vm_execute_program(&program, stdout);
    deus_program_free(&program);
    return result;
}
