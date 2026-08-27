# DEUS Host ABI v1

The host ABI is the boundary between the bounded DEUS VM and an embedding
application such as The Truth. It is versioned independently from the DEUSB
bytecode ABI.

## Current contract

An embedder supplies a `DeusHost` to
`deus_vm_execute_program_with_host`. Before loading programs, it can validate
that host with `deus_host_validate`.

Host ABI v1 currently grants one authority:

- `DEUS_HOST_CAP_NETWORK`: permits `HUNT`, `FORK`, and `HUNT_VALUE` through the
  host's `hunt` callback.

The module named by `OMNI` does not grant authority. The capability bit must be
present and a `hunt` callback must be supplied.

## Ownership

On a successful `hunt`, the host returns a borrowed `DeusHostDocument`. The VM
copies its bytes within the 32 MiB response limit. When `release_document` is
present, the VM invokes it exactly once after inspecting or copying a successful
document. The host owns cleanup when `hunt` reports failure.

Callbacks used by concurrent retrieval must be thread-safe. The `context`
pointer remains owned by the embedder and must outlive the execution.

## Compatibility rules

- `abi_version` must equal `DEUS_HOST_ABI_VERSION`.
- Unknown capability bits are rejected by the current runtime.
- Adding fields to `DeusHost` is not compatible with Host ABI v1.
- New capability families require a new versioned interface or Host ABI v2;
  they must not be appended casually to the v1 struct.
- The bytecode ABI may remain v1 while the host ABI evolves independently.

## The Truth integration boundary

The first The Truth adapter should embed `deuscore` and `deusvmcore`, construct a
validated Host ABI v1 instance, and execute a fixed retrieval program against a
deterministic mock or local source. Index writes, embeddings, ranking, storage,
cancellation, deadlines, and structured output are not part of Host ABI v1 and
must receive explicit contracts before implementation.

## Embeddable output

`deus_vm_execute_program_with_sink` accepts a separately versioned
`DeusOutputSink`. Each `EMIT` forwards UTF-8 or compact JSON bytes to its `write`
callback. Returning zero stops execution with failure. The callback is borrowed
for the duration of the call and the embedder owns its context.

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
timeouts today. Propagating cancellation into active futures and host effects
remains part of the later structured execution contract.
