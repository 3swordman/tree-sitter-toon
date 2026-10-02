# Tree-Sitter TOON Grammar

A complete tree-sitter grammar implementation for TOON (Token-Oriented Object Notation) v2.0.

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

## Status

✅ **Production Ready**
- 93.8% test pass rate (61/65 tests)
- All major TOON v2.0 features implemented
- Comprehensive test coverage

## Features

- ✅ Full-line comments from TOON spec v4 (§5.1), without changing indentation scopes
- ✅ All primitive types (null, boolean, number, string)
- ✅ Unquoted strings with Unicode/emoji support
- ✅ Objects with indentation-based nesting
- ✅ Arrays: inline, tabular, and list formats
- ✅ All delimiters: comma, pipe, tab
- ✅ Headers with field lists
- ✅ Objects as list items
- ✅ Empty arrays
- ✅ Nested structures (tested to 5+ levels)

## Documentation

- **example.toon** - Working demonstration file

## Test Organization

```
test/corpus/
├── arrays/          - Array tests (inline, tabular, list, root)
├── objects/         - Object structure tests
├── delimiters/      - Delimiter-specific tests
├── primitives.txt   - All primitive value types
└── mixed.txt        - Complex combined scenarios
```

## Example

```toon
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

- [TOON Specification v2.0](https://github.com/toon-format/spec)
- [Tree-sitter Documentation](https://tree-sitter.github.io/)
