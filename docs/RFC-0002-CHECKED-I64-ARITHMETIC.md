# RFC-0002: Checked `I64` arithmetic

- Status: Proposed
- Scope: expression arithmetic only
- Source compatibility: additive
- Bytecode ABI impact: additive; requires a new bytecode ABI version
- Host ABI impact: none

## Purpose

DEUS needs deterministic integer arithmetic before it can support transparent scoring, bounded collection ranking, or useful pure rules. This RFC admits the smallest useful subset: checked signed 64-bit arithmetic in pure expressions.

It deliberately does **not** add floating point, implicit numeric conversion, general statements, loops, a `score` keyword, or collection processing.

## Surface syntax and precedence

The following operators are admitted for operands of type `I64`:

```deus
bind title_score = 80
bind evidence_bonus = 15
bind total = title_score + evidence_bonus * 2
bind remainder = total % 10
bind eligible = total >= 100
```

Precedence, from loosest to tightest, becomes:

1. `??`
2. `or`
3. `and`
4. equality (`==`, `!=`)
5. ordering (`<`, `<=`, `>`, `>=`)
6. addition and subtraction (`+`, `-`)
7. multiplication, division, and remainder (`*`, `/`, `%`)
8. unary `not`
9. literals, locals, conversions, and parentheses

Unary numeric negation is intentionally out of scope. Negative values remain available through signed integer literals, as they are today.

## Static type contract

- Every arithmetic operand must be statically `I64`.
- `Value` must be explicitly narrowed with `i64(value)` before arithmetic.
- `Bool`, `String`, `Null`, `Document`, `List`, and `Record` are rejected.
- Each arithmetic expression has type `I64`.
- Arithmetic remains pure: it cannot call an adapter, hunt, reap, mutate a collection, or create a future.

Examples:

```deus
bind raw = call "catalog.score" query
bind score = i64(raw)
bind boosted = score + 10
```

```deus
bind invalid = true + 1       # compile error: expected I64
bind dynamic = raw * 2        # compile error: narrow Value with i64(...)
```

## Runtime contract

Arithmetic is checked, never wrapping and never undefined.

- Overflow or underflow in `+`, `-`, or `*` fails execution.
- `INT64_MIN / -1` and `INT64_MIN % -1` fail execution.
- Division or remainder by zero fails execution.
- Division truncates toward zero, matching C17 signed integer division after the exceptional cases above are handled.
- Remainder has the dividend's sign, matching C17 signed remainder after the exceptional cases above are handled.

The failure message must name the operation, for example `I64 division by zero` or `I64 multiplication overflow`. It is a runtime error rather than an implicit `Null`, saturation, or host-defined behavior.

## Lowering and ABI

The expression AST already represents the five operators. Lowering must emit one stack instruction per operator after its left and right operands:

```text
left + right  -> <left> <right> ADD_I64
left - right  -> <left> <right> SUB_I64
left * right  -> <left> <right> MUL_I64
left / right  -> <left> <right> DIV_I64
left % right  -> <left> <right> MOD_I64
```

New opcode numbers must be allocated in the shared bytecode header, documented in `BYTECODE.md`, validated by the bytecode verifier, and implemented with the same behavior in both native and portable VMs. Because older VMs do not know these instructions, the bytecode ABI version must increase. The host ABI does not change.

No constant folding is required in this increment. If it is introduced later, it must produce the same overflow diagnostics as the VM or leave the operation for runtime evaluation.

## Non-goals and follow-ups

- A `score` statement is not added. Named score contributions should be designed only after arithmetic is stable.
- `rule name(subject): expression` stays a separate RFC; this work merely provides its numeric building block.
- `parallel:` stays deferred; arithmetic does not alter concurrency semantics.
- Provenance remains an explicit typed record/host-contract concern. Numeric scores do not make provenance trusted.
- Floats, decimal arithmetic, rounding, and locale formatting are excluded.

## Acceptance criteria

The implementation is complete only when all of the following hold:

1. Parser and grammar tests prove the precedence table and parentheses.
2. Semantic tests reject every non-`I64` operand and direct use of `Value`.
3. Compiler tests assert exact opcode order for all five operators.
4. Bytecode validation rejects malformed arithmetic stack programs and accepts valid ones with the new ABI version.
5. Native and portable VM tests agree for positive values, negative values, zero, division truncation, and remainder sign.
6. Native and portable VM tests fail deterministically for every overflow boundary and for zero division/remainder.
7. Existing source and prior bytecode compatibility tests remain green under their declared ABI versions.
8. Documentation, TextMate grammar, LSP hover/completion, release notes, and at least one non-EDEN example ship with the implementation.

## Implementation sequence

1. Freeze opcode values and bump the bytecode ABI contract.
2. Add semantic typing and lowering, keeping every failure source-spanned.
3. Implement checked operations in both VMs and bytecode validation.
4. Add adversarial boundary tests before examples or tooling polish.
5. Update public language and bytecode documentation in the same change.

This is intentionally a small language increment: it gives DEUS reliable numeric composition without prematurely committing it to a scoring DSL.
