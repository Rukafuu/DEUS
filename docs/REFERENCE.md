# Reference

The reference is split by contract:

- [source grammar](GRAMMAR.ebnf): lexical and syntactic forms;
- [bytecode ABI](BYTECODE.md): physical `.deusb` encoding;
- [design constitution](../DESIGN.md): language, VM and host boundaries;
- [project manifest](PROJECTS.md): package metadata and capabilities;
- public C headers in `include/`: VM and host embedding API.

Semantic rules such as immutable locals, exactly one `genesis`, terminal
`halt`, type compatibility, capability checks and resource limits are enforced
after parsing and are not duplicated as syntax productions.
