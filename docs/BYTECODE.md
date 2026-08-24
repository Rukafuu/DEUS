# DEUS bytecode ABI v1

Technical reference for the physical `.deusb` format. Language usage and design
rules live in the repository README and `DESIGN.md`.

## Container

All integers are little-endian. The fixed header is 40 bytes:

```text
0x00  byte[8] magic = 44 45 55 53 42 00 01 00
0x08  u16     ABI version
0x0A  u16     flags
0x0C  u32     string count
0x10  u32     string section offset
0x14  u32     string section length
0x18  u32     bytecode offset
0x1C  u32     bytecode length
0x20  u32     payload CRC32
0x24  u32     reserved
```

Each interned string is `u32 length` followed by UTF-8 bytes. Readers validate
the magic, ABI, offsets, section limits, CRC32, opcodes, string indices, local
slots, fixed operands, and that both v1 header fields `flags` and `reserved`
remain zero before execution.

## Opcode alphabet

| Byte | Mnemonic | Operand | Purpose or stack effect |
| --- | --- | --- | --- |
| `0x01` | `OMNI` | string index | declare module |
| `0x02` | `GENESIS` | — | start execution |
| `0x03` | `HUNT` | URL index | `[] → [Document]` |
| `0x04` | `REAP` | selector index | `[Document] → [String]` |
| `0x05` | `HALT` | — | terminate |
| `0x06` | `EMIT` | — | `[Value] → []` |
| `0x07` | `FORK` | URL index | `[] → [Future]` |
| `0x08` | `AWAIT` | — | `[Future] → [Document]` |
| `0x09` | `JOIN` | count | ordered future join |
| `0x0A` | `LIMIT` | workers | concurrency limit |
| `0x0B` | `RETRY` | attempts | retry limit |
| `0x0C` | `BACKOFF` | milliseconds | backoff base |
| `0x0D` | `RATE` | requests/s | global limiter |
| `0x0E` | `CONST` | string index | `[] → [String]` |
| `0x0F` | `BIND` | local slot | `[Value] → []` |
| `0x10` | `LOAD` | local slot | `[] → [Value]` |
| `0x11` | `CONST_NULL` | — | `[] → [Null]` |
| `0x12` | `CONST_BOOL` | `0` or `1` | `[] → [Bool]` |
| `0x13` | `CONST_I64` | signed `i64` | `[] → [I64]` |
| `0x14` | `URL_ENCODE` | — | `[Scalar] → [String]` |
| `0x15` | `URL_JOIN` | — | `[String, String] → [String]` |
| `0x16` | `HUNT_VALUE` | — | `[String] → [Document]` |
| `0x17` | `JSON_PATH` | path index | `[Document] → [Scalar]` |
| `0x18` | `CONST_RECORD` | — | `[] → [Record]` |
| `0x19` | `CONST_LIST` | — | `[] → [List]` |
| `0x1A` | `RECORD_SET` | key index | mutate shared record |
| `0x1B` | `LIST_PUSH` | — | mutate shared list |
| `0x1C` | `RECORD_GET` | key index | `[Record] → [Value]` |
| `0x1D` | `LIST_AT` | index | `[List] → [Value]` |
| `0x1E` | `RECORD_GET_OPTIONAL` | key index | missing becomes `Null` |
| `0x1F` | `LIST_AT_OPTIONAL` | index | missing becomes `Null` |
| `0x20` | `EQUAL` | — | `[Scalar, Scalar] → [Bool]` |
| `0x21` | `NOT_EQUAL` | — | `[Scalar, Scalar] → [Bool]` |
| `0x22` | `LESS` | — | `[I64, I64] → [Bool]` |
| `0x23` | `LESS_EQUAL` | — | `[I64, I64] → [Bool]` |
| `0x24` | `GREATER` | — | `[I64, I64] → [Bool]` |
| `0x25` | `GREATER_EQUAL` | — | `[I64, I64] → [Bool]` |
| `0x26` | `BOOL_AND` | — | `[Bool, Bool] → [Bool]` |
| `0x27` | `BOOL_OR` | — | `[Bool, Bool] → [Bool]` |
| `0x28` | `BOOL_NOT` | — | `[Bool] → [Bool]` |
| `0x29` | `COALESCE` | — | `[Value, Value] → [Value]` |
| `0x2A` | `TO_TEXT` | — | scalar to `String` |
| `0x2B` | `TO_I64` | — | scalar to `I64` |
| `0x2C` | `TO_BOOL` | — | scalar to `Bool` |

Programs may define at most 256 local slots. `BIND` transfers ownership into an
empty slot; `LOAD` pushes an independent copy. Uninitialized slots and rebinding
are rejected.

## Versioning

Language milestones, releases, the host ABI, and this physical ABI are versioned
independently. Additive opcodes may remain ABI v1 while old programs remain valid.
Readers reject unknown or malformed instructions.
