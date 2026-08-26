#include "deus.h"

#include <stdio.h>

static int reject(char *error, size_t error_cap, const char *message) {
    if (error && error_cap) snprintf(error, error_cap, "%s", message);
    return 0;
}

int deus_host_validate(const DeusHost *host, uint64_t required_capabilities,
                       char *error, size_t error_cap) {
    const uint64_t known_capabilities = DEUS_HOST_CAP_NETWORK;

    if (error && error_cap) error[0] = '\0';
    if (!host) return reject(error, error_cap, "host is required");
    if (host->abi_version != DEUS_HOST_ABI_VERSION)
        return reject(error, error_cap, "unsupported host ABI version");
    if (host->capabilities & ~known_capabilities)
        return reject(error, error_cap, "host declares unknown capabilities");
    if ((required_capabilities & ~known_capabilities) != 0u)
        return reject(error, error_cap, "runtime requires unknown capabilities");
    if ((host->capabilities & required_capabilities) != required_capabilities)
        return reject(error, error_cap, "host does not grant required capabilities");
    if ((host->capabilities & DEUS_HOST_CAP_NETWORK) && !host->hunt)
        return reject(error, error_cap,
                      "network capability requires a hunt callback");
    return 1;
}
