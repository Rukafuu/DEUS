#!/bin/bash
# Fix all Value struct initializations in deusvm.c

# Fix lines with missing managed field
sed -i 's/stack\[sp++\] = (Value){V_BOOL, NULL, 0u, NULL, in\.operand ? 1 : 0};/stack[sp++] = (Value){V_BOOL, NULL, 0u, NULL, in.operand ? 1 : 0, {0}};/g' src/deusvm.c
sed -i 's/stack\[sp++\] = (Value){V_I64, NULL, 0u, NULL, in\.immediate};/stack[sp++] = (Value){V_I64, NULL, 0u, NULL, in.immediate, {0}};/g' src/deusvm.c
sed -i 's/stack\[sp++\] = (Value){V_BOOL, NULL, 0u, NULL, result};/stack[sp++] = (Value){V_BOOL, NULL, 0u, NULL, result, {0}};/g' src/deusvm.c
sed -i 's/stack\[sp++\] = (Value){V_STRING, joined, joined_length, NULL, 0};/stack[sp++] = (Value){V_STRING, joined, joined_length, NULL, 0, {0}};/g' src/deusvm.c
sed -i 's/stack\[sp++\] = (Value){V_DOCUMENT, body, length, NULL, 0};/stack[sp++] = (Value){V_DOCUMENT, body, length, NULL, 0, {0}};/g' src/deusvm.c
sed -i 's/stack\[sp++\] = (Value){V_STRING, scalar\.string, scalar\.string_length, NULL, 0};/stack[sp++] = (Value){V_STRING, scalar.string, scalar.string_length, NULL, 0, {0}};/g' src/deusvm.c
sed -i 's/stack\[sp++\] = (Value){V_DOCUMENT, body, len, NULL};/stack[sp++] = (Value){V_DOCUMENT, body, len, NULL, 0, {0}};/g' src/deusvm.c
sed -i 's/stack\[sp++\] = (Value){V_FUTURE, NULL, 0, task};/stack[sp++] = (Value){V_FUTURE, NULL, 0, task, 0, {0}};/g' src/deusvm.c
sed -i 's/stack\[sp++\] = (Value){V_STRING, copy, arg_length, NULL};/stack[sp++] = (Value){V_STRING, copy, arg_length, NULL, 0, {0}};/g' src/deusvm.c
sed -i 's/stack\[sp++\] = (Value){V_TEXT, text, len, NULL};/stack[sp++] = (Value){V_TEXT, text, len, NULL, 0, {0}};/g' src/deusvm.c
