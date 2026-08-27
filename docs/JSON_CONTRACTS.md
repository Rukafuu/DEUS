# JSON Scalar Contracts

`json document "$.path"` intentionally remains a dynamic extraction in DEUS: an
external document does not become trusted merely because it parses as JSON.

Hosts that know the expected response shape can validate scalar fields before
handing a document to a flow. This is an additive C API; it changes neither
DEUS source syntax nor the existing `json` opcode.

```c
static const DeusJsonScalarContract search_result[] = {
    {"$.results[0].title", 18u, DEUS_JSON_STRING, 0},
    {"$.results[0].year", 17u, DEUS_JSON_I64, 0},
    {"$.results[0].available", 22u, DEUS_JSON_BOOL, 0},
    {"$.results[0].subtitle", 21u, DEUS_JSON_STRING, 1}
};

if (!deus_json_validate_scalar_contract(body, body_length, search_result,
                                        sizeof(search_result) / sizeof(search_result[0]),
                                        error, sizeof(error))) {
    /* reject or classify the upstream response */
}
```

Each contract entry requires a JSONPath, an expected scalar kind, and whether
`null` is acceptable. A missing path, a compound value, malformed UTF-8, or a
scalar of another kind fails validation with the field number and expected
kind. `nullable` accepts only JSON `null`; it does not make a path optional.

This establishes a safe boundary contract. A later language-level shape system
can compile declared fields to the same contract without changing the runtime
meaning of the existing `json` expression.