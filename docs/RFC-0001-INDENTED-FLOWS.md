# RFC-0001: Indented flows

- Status: Accepted, Phase 1 implemented
- Source compatibility: additive
- Bytecode impact: none
- Physical ABI impact: none

## Motivation

Flat instruction sequences are sufficient for the original crawler prototype but
do not communicate ownership once DEUS grows into flows, policies, rules,
structured concurrency, filters, ranking, and authorized sinks. Significant
indentation becomes part of the language's information architecture rather than
visual decoration.

## Modern entry form

```deus
flow research:
    bind score = 95
    bind eligible = score >= 80

    load eligible
    emit
```

Phase 1 supports one named top-level `flow`. The frontend lowers its header to
`GENESIS` and appends `HALT`; its body lowers through the existing parser,
semantic analyzer, and ABI v1 generator.

## Layout contract

1. A flow header has the form `flow <identifier>:` at column one.
2. Non-empty body lines begin with at least four spaces.
3. Indentation uses multiples of four spaces.
4. Tabs in leading indentation are errors.
5. Blank and comment-only lines do not create or close blocks.
6. Indentation inside `{}`, `[]`, and future parenthesized multiline expressions
   may be deeper without changing the surrounding flow owner.
7. Diagnostics retain original source line numbers.
8. Source size remains bounded by the compiler's existing section and allocation
   limits.

## Compatibility

Flat programs containing explicit `genesis` and `halt` remain accepted during the
migration period. They compile to the same bytecode as before. New documentation
and examples should prefer the `flow` form.

The compatibility form is not a second semantic language. Both source forms lower
to the same typed AST and bytecode primitives. A future source-language major
version may remove explicit lifecycle instructions after migration tooling exists.

## Diagnostics

The compiler rejects:

- missing flow bodies;
- body statements indented by fewer than four spaces;
- indentation widths that are not multiples of four;
- tabs in indentation;
- malformed flow names or missing `:` through the ordinary parser diagnostic.

## Planned block families

The layout contract is intentionally shared by future domain blocks:

```deus
flow research:
    limits:
        concurrency 8
        retry 3

    parallel:
        bind web = hunt web_source
        bind api = hunt api_source

    rule trustworthy(candidate):
        candidate.score >= 80
        and candidate.provenance.verified

    emit jsonl candidates into index
```

These examples reserve direction, not current syntax. Each block requires its own
RFC for types, effects, failure semantics, resource budgets, and capability
requirements before admission.

## Non-goals

- arbitrary nested imperative scopes;
- indentation-based general-purpose control flow;
- changes to the bytecode container;
- silently accepting mixed tabs and spaces;
- making visual indentation meaningful inside legacy flat programs.
