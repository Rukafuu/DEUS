#include "deus.h"

#include <stdio.h>
#include <string.h>

static int mock_hunt(void *context, const char *url, size_t url_length,
                     DeusHostDocument *document, char *error,
                     size_t error_cap) {
    (void)context;
    (void)url;
    (void)url_length;
    (void)document;
    (void)error;
    (void)error_cap;
    return 0;
}

static int expect_rejection(const char *name, const DeusHost *host,
                            uint64_t required, const char *message) {
    char error[192] = {0};
    if (deus_host_validate(host, required, error, sizeof(error))) {
        fprintf(stderr, "host ABI test [%s]: host was accepted\n", name);
        return 0;
    }
    if (!strstr(error, message)) {
        fprintf(stderr, "host ABI test [%s]: expected '%s', got '%s'\n",
                name, message, error);
        return 0;
    }
    return 1;
}

int main(void) {
    DeusHost valid = {
        .abi_version = DEUS_HOST_ABI_VERSION,
        .capabilities = DEUS_HOST_CAP_NETWORK,
        .context = NULL,
        .hunt = mock_hunt,
        .release_document = NULL,
        .call = NULL,
    };
    DeusHost wrong_version = valid;
    DeusHost missing_callback = valid;
    DeusHost unknown_capability = valid;
    char error[192] = {0};

    wrong_version.abi_version++;
    missing_callback.hunt = NULL;
    unknown_capability.capabilities |= UINT64_C(1) << 63;

    if (!deus_host_validate(&valid, DEUS_HOST_CAP_NETWORK,
                            error, sizeof(error))) {
        fprintf(stderr, "host ABI test [valid]: %s\n", error);
        return 1;
    }
    if (!expect_rejection("null", NULL, 0u, "host is required") ||
        !expect_rejection("version", &wrong_version, 0u,
                          "unsupported host ABI version") ||
        !expect_rejection("grant", &valid, UINT64_C(1) << 63,
                          "runtime requires unknown capabilities") ||
        !expect_rejection("callback", &missing_callback, 0u,
                          "requires a hunt callback") ||
        !expect_rejection("unknown", &unknown_capability, 0u,
                          "unknown capabilities"))
        return 1;

    puts("host ABI v1 validation passed");
    return 0;
}
