# Tree-Sitter TOON Grammar

A tree-sitter grammar targeting **TOON specification 4.4** (October 8, 2026), with syntax highlighting queries for editors including Zed (`toon-spec: 4.4`).

## Installation

### Python

```bash
pip install tree-sitter-toon
```

```python
from tree_sitter import Language, Parser
from tree_sitter_toon import language

parser = Parser(Language(language()))
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
- Arbitrary literal header and field names, empty keys, and spaces before key-value colons
- Quoted spans inside unquoted tokens, literal quoted controls, BOM and CRLF handling
- Objects, list items, quoted strings, numbers, booleans, and null
- Correct sibling scopes after empty objects and list-item first fields
- Highlighting, bracket matching, and indentation queries

This grammar produces syntax trees for editor tooling. It is not a TOON decoder
or strict validator: declared lengths, row widths, duplicate keys, numeric range,
and configurable strict indentation checks belong in a decoder or validator.
Indentation widths are inferred from nested scopes, including custom widths.
Blank lines and some non-strict indentation forms remain accepted for editing.
Dotted keys are literal keys; v4 removes key folding and path expansion.

Malformed headers such as `foo[03]: 1` are syntax errors under 4.4, including
in non-strict mode. Quote the key (`"foo[03]": 1`) to use it literally. Tables
require field lists, and keyless fields-bearing headers are allowed only at the
document root. An empty unquoted key has no `key` child in its `pair` or
`keyed_row`, just as empty cells have no `value` child.

## Verification

```bash
tree-sitter test
python3 test/check_v4.py
# Corpus, native Node binding, and editor query checks
npm test
```

The corpus includes all **246 valid decode cases and 160 encoder outputs** from
the pinned TOON spec `v4.4.0` tag (revision
`d09c3d33e37389345a74bd96981eb96dd9be6c5e`). Exact-input checks preserve missing
final newlines and control characters, reconstruct fixture data from the syntax
trees to check scopes, and verify malformed syntax, token classification, custom
indentation, and incremental edits. Fixture sources and their MIT license are in
`test/fixtures/`. The Node binding requires the Tree-sitter 0.25.x runtime.

To refresh the pinned fixtures and corpus snapshots from a spec checkout:

```bash
python3 test/update_spec_fixtures.py /path/to/toon-format/spec
```

Review the generated snapshots and run the checks above after importing.

## Documentation

- **example.toon** - Working demonstration file

## Test Organization

```
test/corpus/
├── arrays/          - Array tests (inline, tabular, list, root)
├── objects/         - Object structure tests
├── delimiters/      - Delimiter-specific tests
├── spec/            - Pinned TOON v4.4 decode fixtures and encoder outputs
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

- [TOON Specification v4.4](https://github.com/toon-format/spec/blob/v4.4.0/SPEC.md)
- [Tree-sitter Documentation](https://tree-sitter.github.io/)
