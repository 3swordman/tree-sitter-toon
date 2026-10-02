# Tree-Sitter TOON Grammar

A tree-sitter grammar for TOON (Token-Oriented Object Notation) v4.0, with syntax highlighting queries for editors including Zed.

## Installation

### Python

```bash
pip install tree-sitter-toon
```

```python
from tree_sitter import Parser
from tree_sitter_toon import language

parser = Parser(language())
tree = parser.parse(b"name: value")
```

### Development

```bash
# Generate parser
tree-sitter generate

# Run tests
tree-sitter test

# Parse a file
tree-sitter parse example.toon
```

## Features

- Full-line comments that preserve indentation scopes
- Arrays with comma, pipe, or tab delimiters and nested delimiter scopes
- Keyed tabular objects, including root keyed tables
- Recursive field groups in tabular and keyed headers
- Bare empty arrays in fields, at the root, and in list items
- Decoder-compatible literal keys and unquoted strings, Unicode, and `\uXXXX` escapes
- Objects, list items, quoted strings, numbers, booleans, and null
- Highlighting, bracket matching, and indentation queries

This grammar produces syntax trees for editor tooling. It is not a TOON decoder
or strict validator: declared lengths, row widths, duplicate keys, numeric range,
and configurable strict indentation checks belong in a decoder or validator.
Some non-strict input forms remain accepted for editing. Dotted keys are literal
keys; v4 removes key folding and path expansion.

## Verification

```bash
tree-sitter test
python3 test/check_v4.py
```

The corpus includes valid decode fixtures from the pinned TOON spec `v4.0.0`
tag. The additional checks preserve exact input bytes (including missing final
newlines), verify delimiter and token classification, and exercise incremental
edits. Fixture sources and their MIT license are in `test/fixtures/`.

## Documentation

- **example.toon** - Working demonstration file

## Test Organization

```
test/corpus/
├── arrays/          - Array tests (inline, tabular, list, root)
├── objects/         - Object structure tests
├── delimiters/      - Delimiter-specific tests
├── spec/            - Pinned TOON v4.0 decode fixtures
├── comments.txt     - Comment and CRLF regressions
├── primitives.txt   - All primitive value types
└── mixed.txt        - Complex combined scenarios
```

## Example

```toon
# Keyed tables can share a nested field structure.
servers[2:]{host,geo{lat,lon}}:
  alpha: a.example.com,50,10
  beta: b.example.com,40,-100

name: TOON Parser
version: 2.0
unicode: Hello 世界 🎉

arrays:
  inline[3]: a,b,c
  empty[0]:
  
table[2]{id,name}:
  1,Alice
  2,Bob

users[2]:
  - name: Alice
    score: 100
  - name: Bob
    score: 200

nested:
  data[2]: x,y
```

## License

Licensed under the [MIT License](LICENSE).

## Links

- [TOON Specification v4.0](https://github.com/toon-format/spec/blob/v4.0.0/SPEC.md)
- [Tree-sitter Documentation](https://tree-sitter.github.io/)
