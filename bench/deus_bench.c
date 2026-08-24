#include "deus.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#else
#include <time.h>
#endif

#define CHECK(condition, message) do { if (!(condition)) { \
    fprintf(stderr, "benchmark failed: %s\n", message); return 1; \
} } while (0)

static double now_ms(void) {
#ifdef _WIN32
    LARGE_INTEGER counter, frequency;
    QueryPerformanceCounter(&counter); QueryPerformanceFrequency(&frequency);
    return (double)counter.QuadPart * 1000.0 / (double)frequency.QuadPart;
#else
    return (double)clock() * 1000.0 / (double)CLOCKS_PER_SEC;
#endif
}

static double peak_memory_mib(void) {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS counters;
    if (!GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))) return 0.0;
    return (double)counters.PeakWorkingSetSize / (1024.0 * 1024.0);
#else
    return 0.0;
#endif
}

static char *scalar_source(unsigned bindings) {
    size_t capacity = 64u + (size_t)bindings * 40u, used = 0u; char *source = (char *)malloc(capacity);
    if (!source) return NULL;
    used += (size_t)snprintf(source + used, capacity - used, "genesis\n");
    for (unsigned index = 0u; index < bindings; index++) {
        int written = snprintf(source + used, capacity - used, "bind value_%03u = %u\n", index, index);
        if (written < 0 || (size_t)written >= capacity - used) { free(source); return NULL; }
        used += (size_t)written;
    }
    if (snprintf(source + used, capacity - used, "halt\n") < 0) { free(source); return NULL; }
    return source;
}

static void metric(const char *name, unsigned iterations, double elapsed_ms, unsigned long long units) {
    printf("%-28s iterations=%-6u total_ms=%9.3f us_per_iter=%9.3f",
           name, iterations, elapsed_ms, elapsed_ms * 1000.0 / (double)iterations);
    if (units) printf(" units_per_second=%llu", (unsigned long long)((double)units * 1000.0 / elapsed_ms));
    fputc('\n', stdout);
}

int main(void) {
    const unsigned bindings = 200u, compile_iterations = 500u;
    const unsigned bytecode_iterations = 250u, vm_iterations = 1000u;
    const char *bytecode_path = "deus_benchmark.deusb"; char error[192];
    char *source = scalar_source(bindings); size_t source_length; double started, elapsed;
    DeusProgram program, decoded; DeusDiagnostic diagnostic = {0}; FILE *sink;
    CHECK(source, "source allocation"); source_length = strlen(source);

    started = now_ms();
    for (unsigned iteration = 0u; iteration < compile_iterations; iteration++) {
        CHECK(deus_parse_source(source, source_length, &program, &diagnostic), diagnostic.message);
        deus_program_free(&program);
    }
    elapsed = now_ms() - started;
    printf("DEUS benchmark protocol 1\n");
    printf("workload bindings=%u source_bytes=%zu\n", bindings, source_length);
    metric("compile_200_bindings", compile_iterations, elapsed, 0u);

    CHECK(deus_parse_source(source, source_length, &program, &diagnostic), diagnostic.message);
    started = now_ms();
    for (unsigned iteration = 0u; iteration < bytecode_iterations; iteration++) {
        CHECK(deus_write_binary(&program, bytecode_path, error, sizeof(error)), error);
        CHECK(deus_read_binary(bytecode_path, &decoded, error, sizeof(error)), error);
        deus_program_free(&decoded);
    }
    elapsed = now_ms() - started;
    metric("bytecode_write_read", bytecode_iterations, elapsed, 0u);
    (void)remove(bytecode_path);

#ifdef _WIN32
    CHECK(tmpfile_s(&sink) == 0 && sink, "temporary output stream");
#else
    sink = tmpfile(); CHECK(sink, "temporary output stream");
#endif
    started = now_ms();
    for (unsigned iteration = 0u; iteration < vm_iterations; iteration++)
        CHECK(deus_vm_execute_program(&program, sink) == 0, "VM execution");
    elapsed = now_ms() - started;
    metric("vm_scalar_instructions", vm_iterations, elapsed,
           (unsigned long long)program.code_count * vm_iterations);
    fclose(sink);

    printf("program instructions=%u strings=%u\n", program.code_count, program.string_count);
    printf("peak_working_set_mib=%.3f\n", peak_memory_mib());
    deus_program_free(&program); free(source); return 0;
}
