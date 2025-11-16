#include "tree_sitter/parser.h"
#include "tree_sitter/array.h"
#include <wctype.h>

enum TokenType {
  INDENT,
  DEDENT,
};

typedef struct Scanner {
  Array(uint32_t) indents;
} Scanner;

static inline void advance(TSLexer *lexer) { lexer->advance(lexer, false); }
static inline void skip(TSLexer *lexer) { lexer->advance(lexer, true); }

void *tree_sitter_toon_external_scanner_create() {
  Scanner *scanner = (Scanner *)calloc(1, sizeof(Scanner));
  array_init(&scanner->indents);
  array_push(&scanner->indents, 0);
  return scanner;
}

void tree_sitter_toon_external_scanner_destroy(void *payload) {
  Scanner *scanner = (Scanner *)payload;
  array_delete(&scanner->indents);
  free(scanner);
}

unsigned tree_sitter_toon_external_scanner_serialize(void *payload, char *buffer) {
  Scanner *scanner = (Scanner *)payload;
  size_t size = scanner->indents.size;
  
  if (size > TREE_SITTER_SERIALIZATION_BUFFER_SIZE / sizeof(uint32_t)) {
    size = TREE_SITTER_SERIALIZATION_BUFFER_SIZE / sizeof(uint32_t);
  }
  
  memcpy(buffer, scanner->indents.contents, size * sizeof(uint32_t));
  return size * sizeof(uint32_t);
}

void tree_sitter_toon_external_scanner_deserialize(void *payload, const char *buffer, unsigned length) {
  Scanner *scanner = (Scanner *)payload;
  array_clear(&scanner->indents);
  
  if (length == 0) {
    array_push(&scanner->indents, 0);
    return;
  }
  
  size_t size = length / sizeof(uint32_t);
  for (size_t i = 0; i < size; i++) {
    uint32_t indent;
    memcpy(&indent, buffer + (i * sizeof(uint32_t)), sizeof(uint32_t));
    array_push(&scanner->indents, indent);
  }
}

static bool scan_whitespace(TSLexer *lexer, uint32_t *indent_length) {
  *indent_length = 0;
  
  while (true) {
    if (lexer->lookahead == ' ') {
      (*indent_length)++;
      skip(lexer);
    } else if (lexer->lookahead == '\t') {
      // Tabs count as moving to next tab stop (every 2 spaces for TOON)
      (*indent_length) = ((*indent_length) + 2) & ~1;
      skip(lexer);
    } else {
      break;
    }
  }
  
  return true;
}

bool tree_sitter_toon_external_scanner_scan(void *payload, TSLexer *lexer, const bool *valid_symbols) {
  Scanner *scanner = (Scanner *)payload;
  
  // Skip blank lines
  bool has_newline = false;
  while (lexer->lookahead == '\n' || lexer->lookahead == '\r') {
    has_newline = true;
    if (lexer->lookahead == '\r') {
      skip(lexer);
      if (lexer->lookahead == '\n') {
        skip(lexer);
      }
    } else {
      skip(lexer);
    }
    
    // Skip blank lines (lines with only whitespace)
    while (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
      skip(lexer);
    }
    if (lexer->lookahead == '\n' || lexer->lookahead == '\r') {
      continue;
    }
    break;
  }
  
  // Handle indentation at start of line
  if (has_newline || lexer->get_column(lexer) == 0) {
    uint32_t indent_length = 0;
    
    while (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
      if (lexer->lookahead == ' ') {
        indent_length++;
      } else {
        // Tab counts as moving to next tab stop (every 2 spaces)
        indent_length = (indent_length + 2) & ~1;
      }
      skip(lexer);
    }
    
    // End of file
    if (lexer->eof(lexer)) {
      if (valid_symbols[DEDENT] && scanner->indents.size > 1) {
        array_pop(&scanner->indents);
        lexer->result_symbol = DEDENT;
        return true;
      }
      return false;
    }
    
    // Empty line
    if (lexer->lookahead == '\n' || lexer->lookahead == '\r') {
      return false;
    }
    
    uint32_t current_indent = *array_back(&scanner->indents);
    
    // Indent - always prefer INDENT over other tokens when indentation increases
    if (indent_length > current_indent && valid_symbols[INDENT]) {
      array_push(&scanner->indents, indent_length);
      lexer->result_symbol = INDENT;
      return true;
    }
    
    // Dedent
    if (indent_length < current_indent && valid_symbols[DEDENT]) {
      array_pop(&scanner->indents);
      lexer->result_symbol = DEDENT;
      return true;
    }
  }
  
  return false;
}
