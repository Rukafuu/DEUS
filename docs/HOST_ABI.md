# DEUS Host ABI v2

The host ABI is the boundary between the bounded DEUS VM and an embedding
application. It is versioned independently from the DEUSB bytecode ABI.

## Current contract

An embedder supplies a `DeusHost` to `deus_vm_execute_program_with_host`. Before
loading programs, it can validate that host with `deus_host_validate`.

Host ABI v2 defines two explicit capability families:

- `DEUS_HOST_CAP_NETWORK`: permits `HUNT`, `FORK`, and `HUNT_VALUE` through the
  host's `hunt` callback;
- `DEUS_HOST_CAP_ADAPTER_CALL`: permits `call "adapter.operation"` through the
  host's `call` callback.

Naming a module or adapter does not grant authority. The matching capability
bit and callback must both be present. Adapter hosts remain responsible for
input validation, budgets, authentication, allowlists, and the authority of
any filesystem, network, model, or database integration.

## Ownership

On a successful `hunt`, the host returns a borrowed `DeusHostDocument`. The VM
copies its bytes within the 32 MiB response limit. When `release_document` is
present, the VM invokes it exactly once after inspecting or copying a
successful document. The host owns cleanup when `hunt` reports failure.

The adapter `call` callback receives borrowed input and a `DeusValueContext` for
its output. The VM copies the returned serializable value into its runtime
representation and disposes the managed result. Contexts and callback state
must outlive the execution that uses them.

Callbacks used by concurrent retrieval must be thread-safe.

## Compatibility rules

- `abi_version` must equal `DEUS_HOST_ABI_VERSION`.
- Unknown capability bits are rejected by the current runtime.
- A capability bit without its required callback is rejected.
- Adding or reordering fields in `DeusHost` requires a new Host ABI version.
- The bytecode ABI evolves independently; DEUS v0.4.0 uses bytecode ABI v4 and
  Host ABI v2.

## Embeddable output

`deus_vm_execute_program_with_sink` accepts a separately versioned
`DeusOutputSink`. Each `EMIT` forwards UTF-8 or compact JSON bytes to its
`write` callback. Returning zero stops execution with failure. The callback is
borrowed for the duration of the call and the embedder owns its context.

The existing `deus_vm_execute_program` and
`deus_vm_execute_program_with_host` functions remain compatibility wrappers
that adapt a `FILE *` to the same sink path.

## Execution controls

`deus_vm_execute_program_with_options` accepts separately versioned
`DeusExecutionOptions`. The default instruction budget is one million decoded
instructions. An embedder may lower it, provide a cooperative cancellation
callback, and set an absolute deadline in the units returned by its monotonic
`now_ms` callback.

Controls are checked before every instruction. They do not preempt a host
callback that is already blocked; hosts must enforce their own transport
timeouts.

See [`HOST_SDK.md`](HOST_SDK.md) for an embedding example and operational
guidance.
