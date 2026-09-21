# DEUS Host SDK

DEUS programs run inside an embedding host. The language never grants authority
by naming an adapter: the host advertises capabilities and decides what every
adapter name means.

## Minimal embedding

Compile source with `deus_parse_source`, then execute it with a `DeusHost`:

```c
DeusHost host = {
    DEUS_HOST_ABI_VERSION,
    DEUS_HOST_CAP_ADAPTER_CALL,
    &application,
    NULL,
    NULL,
    application_call
};
int exit_code = deus_vm_execute_program_with_host(&program, stdout, &host);
```

Use designated initializers where the target compiler supports C17. They make
future ABI migrations easier to review.

## Capabilities

| Capability | Host responsibility |
|---|---|
| `DEUS_HOST_CAP_NETWORK` | Implements `hunt` and releases each borrowed document exactly once. |
| `DEUS_HOST_CAP_ADAPTER_CALL` | Implements the generic `call` callback. |

The VM rejects an operation whose capability or callback is absent. A host may
expose only the capabilities it intends to authorize.

## Generic adapter callback

```c
static int application_call(void *context, const char *name, size_t name_length,
                            const DeusValue *input, DeusValueContext *values,
                            DeusValue *output, char *error, size_t error_cap) {
    if (name_length == 14u && !memcmp(name, "text.normalize", 14u))
        return deus_value_string(values, "normalized", 10u, output);
    snprintf(error, error_cap, "unknown adapter: %.*s", (int)name_length, name);
    return 0;
}
```

`input` is borrowed for the duration of the callback. `output` starts empty;
when returning success, construct it using the provided `values` context. The
VM owns and disposes the result after the callback returns. Do not attach values
allocated from another context.

Adapter inputs and outputs are limited to serializable values: `null`, booleans,
`i64`, UTF-8 strings, lists, and records. Documents, futures, bytes, and errors
are rejected at this boundary.

## Naming and errors

Adapter names have the grammar `[a-z][a-z0-9.-]*`. Names are conventions owned
by the embedding application, not a global registry. Prefer namespaced names
such as `text.normalize`, `storage.lookup`, or `vision.describe`.

Return `0` and write a concise diagnostic to `error` for expected failures,
including unknown adapters, invalid input, quota exhaustion, and unavailable
dependencies. Do not terminate the process from a callback.

## Security boundary

- Validate every adapter input as untrusted program data.
- Apply application-level budgets, authentication, and allowlists in the host.
- Keep secrets and direct provider credentials out of `.deus` source files.
- Treat adapters with filesystem, network, model, or database access as
  privileged capabilities.

## Example programs

`examples/eden_search.deus` demonstrates an application-specific flow over
generic adapters. The language core has no dependency on EDEN, vision models,
or catalog providers.
