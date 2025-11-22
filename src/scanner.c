#include "tree_sitter/parser.h"
#include "tree_sitter/array.h"
#include <wctype.h>
#include <string.h>

enum TokenType {
  INDENT,
  DEDENT,
  NEWLINE,
};

typedef struct Scanner {
  Array(uint32_t) indents;
  bool at_line_start;
} Scanner;

static inline void advance(TSLexer *lexer) { lexer->advance(lexer, false); }
static inline void skip(TSLexer *lexer) { lexer->advance(lexer, true); }

void *tree_sitter_toon_external_scanner_create() {
  Scanner *scanner = (Scanner *)calloc(1, sizeof(Scanner));
  array_init(&scanner->indents);
  array_push(&scanner->indents, 0);
  scanner->at_line_start = true;
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
  
  if (size > (TREE_SITTER_SERIALIZATION_BUFFER_SIZE - 1) / sizeof(uint32_t)) {
    size = (TREE_SITTER_SERIALIZATION_BUFFER_SIZE - 1) / sizeof(uint32_t);
  }
  
  memcpy(buffer, scanner->indents.contents, size * sizeof(uint32_t));
  buffer[size * sizeof(uint32_t)] = (char)scanner->at_line_start;
  return size * sizeof(uint32_t) + 1;
}

void tree_sitter_toon_external_scanner_deserialize(void *payload, const char *buffer, unsigned length) {
  Scanner *scanner = (Scanner *)payload;
  array_clear(&scanner->indents);
  scanner->at_line_start = true;
  
  if (length == 0) {
    array_push(&scanner->indents, 0);
    return;
  }
  
  size_t indent_count = (length - 1) / sizeof(uint32_t);
  for (size_t i = 0; i < indent_count; i++) {
    uint32_t indent;
    memcpy(&indent, buffer + (i * sizeof(uint32_t)), sizeof(uint32_t));
    array_push(&scanner->indents, indent);
  }
  
  if (length > indent_count * sizeof(uint32_t)) {
    scanner->at_line_start = (bool)buffer[length - 1];
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
  
  // Track if we've processed newlines
  bool has_newline = false;
  
  // Skip blank lines and newlines
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
    uint32_t temp_indent = 0;
    while (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
      temp_indent++;
      skip(lexer);
    }
    
    // If we hit another newline, this was a blank line - continue skipping
    if (lexer->lookahead == '\n' || lexer->lookahead == '\r') {
      continue;
    }
    
    // Not a blank line, break to process indentation
    break;
  }
  
  // Handle indentation at start of line
  if (has_newline || lexer->get_column(lexer) == 0) {
    scanner->at_line_start = true;
    uint32_t indent_length = 0;
    
    // Calculate indentation
    while (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
      if (lexer->lookahead == ' ') {
        indent_length++;
      } else {
        // Tab counts as moving to next tab stop (every 2 spaces)
        indent_length = (indent_length + 2) & ~1;
      }
      skip(lexer);
    }
    
    // End of file - emit remaining dedents
    if (lexer->eof(lexer)) {
      if (valid_symbols[DEDENT] && scanner->indents.size > 1) {
        array_pop(&scanner->indents);
        lexer->result_symbol = DEDENT;
        scanner->at_line_start = false;
        return true;
      }
      return false;
    }
    
    // Empty line (shouldn't reach here after blank line skipping, but handle it)
    if (lexer->lookahead == '\n' || lexer->lookahead == '\r') {
      return false;
    }
    
    uint32_t current_indent = *array_back(&scanner->indents);
    
    // Indent - when indentation increases
    if (indent_length > current_indent && valid_symbols[INDENT]) {
      array_push(&scanner->indents, indent_length);
      lexer->result_symbol = INDENT;
      scanner->at_line_start = false;
      return true;
    }
    
    // Dedent - when indentation decreases
    if (indent_length < current_indent && valid_symbols[DEDENT]) {
      // Verify the target indent level exists in the stack
      bool found = false;
      for (int i = scanner->indents.size - 1; i >= 0; i--) {
        if (scanner->indents.contents[i] == indent_length) {
          found = true;
          break;
        }
        if (scanner->indents.contents[i] < indent_length) {
          // Indentation doesn't align with any previous level
          // For strict mode, this would be an error, but we emit dedent anyway
          break;
        }
      }
      
      // Emit one dedent at a time
      array_pop(&scanner->indents);
      lexer->result_symbol = DEDENT;
      scanner->at_line_start = false;
      return true;
    }
    
    // Same indentation level - mark that we're no longer at line start
    scanner->at_line_start = false;
  }
  
  return false;
}
