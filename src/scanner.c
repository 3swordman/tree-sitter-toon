#include "tree_sitter/parser.h"
#include "tree_sitter/array.h"
#include <string.h>

enum TokenType {
  INDENT, DEDENT, COMMENT, NEWLINE, KEY, STRING,
  CELL_COMMA, CELL_PIPE, CELL_TAB,
  FIRST_COMMA, FIRST_PIPE, FIRST_TAB, LIST_MARKER, BLANK_LINE, ENTRY_KEY,
};

typedef struct {
  Array(uint32_t) indents;
  bool eof_newline;
  uint16_t pending_dedents;
  uint32_t pending_indent;
} Scanner;

static void advance(TSLexer *lexer) { lexer->advance(lexer, false); }
static void skip(TSLexer *lexer) { lexer->advance(lexer, true); }

void *tree_sitter_toon_external_scanner_create(void) {
  Scanner *scanner = calloc(1, sizeof(Scanner));
  array_init(&scanner->indents);
  array_push(&scanner->indents, 0);
  return scanner;
}

void tree_sitter_toon_external_scanner_destroy(void *payload) {
  Scanner *scanner = payload;
  array_delete(&scanner->indents);
  free(scanner);
}

unsigned tree_sitter_toon_external_scanner_serialize(void *payload, char *buffer) {
  Scanner *scanner = payload;
  unsigned count = scanner->indents.size;
  unsigned limit = (TREE_SITTER_SERIALIZATION_BUFFER_SIZE - 7) / sizeof(uint32_t);
  if (count > limit) count = limit;
  buffer[0] = scanner->eof_newline;
  memcpy(buffer + 1, &scanner->pending_dedents, 2);
  memcpy(buffer + 3, &scanner->pending_indent, 4);
  memcpy(buffer + 7, scanner->indents.contents, count * sizeof(uint32_t));
  return 7 + count * sizeof(uint32_t);
}

void tree_sitter_toon_external_scanner_deserialize(void *payload, const char *buffer, unsigned length) {
  Scanner *scanner = payload;
  array_clear(&scanner->indents);
  scanner->eof_newline = length ? buffer[0] : false;
  scanner->pending_dedents = 0;
  if (length >= 3) memcpy(&scanner->pending_dedents, buffer + 1, 2);
  scanner->pending_indent = 0;
  if (length >= 7) memcpy(&scanner->pending_indent, buffer + 3, 4);
  for (unsigned offset = 7; offset + sizeof(uint32_t) <= length; offset += sizeof(uint32_t)) {
    uint32_t indent;
    memcpy(&indent, buffer + offset, sizeof(indent));
    array_push(&scanner->indents, indent);
  }
  if (!scanner->indents.size) array_push(&scanner->indents, 0);
}

static void consume_newline(TSLexer *lexer, bool skipped) {
  if (lexer->lookahead == '\r') lexer->advance(lexer, skipped);
  if (lexer->lookahead == '\n') lexer->advance(lexer, skipped);
}

// A small DFA classifies complete unquoted tokens without truncating long numbers.
typedef Array(int32_t) TokenText;

static bool numeric(const TokenText *text) {
  unsigned i = 0;
  if (i < text->size && text->contents[i] == '-') i++;
  unsigned start = i;
  while (i < text->size && text->contents[i] >= '0' && text->contents[i] <= '9') i++;
  if (i == start || (i - start > 1 && text->contents[start] == '0')) return false;
  if (i < text->size && text->contents[i] == '.') {
    start = ++i;
    while (i < text->size && text->contents[i] >= '0' && text->contents[i] <= '9') i++;
    if (i == start) return false;
  }
  if (i < text->size && (text->contents[i] == 'e' || text->contents[i] == 'E')) {
    i++;
    if (i < text->size && (text->contents[i] == '+' || text->contents[i] == '-')) i++;
    start = i;
    while (i < text->size && text->contents[i] >= '0' && text->contents[i] <= '9') i++;
    if (i == start) return false;
  }
  return i == text->size;
}

static bool equals(const TokenText *text, const char *word) {
  if (text->size != strlen(word)) return false;
  for (unsigned i = 0; i < text->size; i++) if (text->contents[i] != word[i]) return false;
  return true;
}

// Look ahead without moving mark_end: a valid header leaves the key token
// ending before '['. Malformed bracket syntax remains a literal decoder key.
static bool scan_header_suffix(TSLexer *lexer) {
  advance(lexer); // '['
  if (lexer->lookahead < '0' || lexer->lookahead > '9') return false;
  bool zero = lexer->lookahead == '0';
  advance(lexer);
  if (zero && lexer->lookahead >= '0' && lexer->lookahead <= '9') return false;
  while (lexer->lookahead >= '0' && lexer->lookahead <= '9') advance(lexer);
  bool keyed = lexer->lookahead == ':';
  if (keyed) advance(lexer);
  if (lexer->lookahead == '|' || lexer->lookahead == '\t') advance(lexer);
  if (lexer->lookahead != ']') return false;
  advance(lexer);
  if (lexer->lookahead == '{') {
    unsigned depth = 0;
    bool quoted = false, escaped = false;
    do {
      int32_t c = lexer->lookahead;
      if (lexer->eof(lexer) || c == '\n' || c == '\r') return false;
      if (!quoted && c == '{') depth++;
      if (!quoted && c == '}') depth--;
      if (escaped) escaped = false;
      else if (quoted && c == '\\') escaped = true;
      else if (c == '"') quoted = !quoted;
      advance(lexer);
    } while (depth);
  } else if (keyed) return false;
  return lexer->lookahead == ':';
}

static bool scan_token(TSLexer *lexer, const bool *valid) {
  if (lexer->eof(lexer) || lexer->lookahead == '"' || lexer->lookahead == '\n' || lexer->lookahead == '\r') return false;
  int symbol = STRING;
  int32_t separator = 0;
  bool first = false;
  for (int i = 0; i < 3; i++) {
    if (valid[FIRST_COMMA + i] || valid[CELL_COMMA + i]) {
      first = valid[FIRST_COMMA + i];
      symbol = first ? FIRST_COMMA + i : CELL_COMMA + i;
      separator = i == 0 ? ',' : i == 1 ? '|' : '\t';
      break;
    }
  }
  if (!separator && !valid[STRING] && !valid[KEY] && !valid[ENTRY_KEY] && !valid[LIST_MARKER]) return false;
  TokenText text;
  array_init(&text);
  bool quoted = false, escaped = false;
  unsigned trimmed = 0;
  bool key_candidate = valid[KEY] || valid[ENTRY_KEY];
  while (!lexer->eof(lexer) && lexer->lookahead != '\n' && lexer->lookahead != '\r') {
    int32_t c = lexer->lookahead;
    if (!quoted && separator && c == separator) break;
    if (valid[LIST_MARKER] && text.size == 1 && text.contents[0] == '-' && c == ' ') {
      while (lexer->lookahead == ' ') advance(lexer);
      lexer->mark_end(lexer);
      array_delete(&text);
      lexer->result_symbol = LIST_MARKER;
      return true;
    }
    if (!quoted && c == ':' && (key_candidate || first)) {
      if (!separator && text.size && text.contents[0] == '[') {
        array_delete(&text);
        return false;
      }
      if (key_candidate && trimmed) {
        array_delete(&text);
        lexer->result_symbol = valid[ENTRY_KEY] ? ENTRY_KEY : KEY;
        return true;
      }
      array_delete(&text);
      return false;
    }
    if (!quoted && c == '[' && valid[KEY]) {
      bool header_key = text.size > 0;
      for (unsigned i = 0; i < text.size; i++) {
        int32_t k = text.contents[i];
        if (!((k >= 'a' && k <= 'z') || (k >= 'A' && k <= 'Z') || k == '_' || (i && ((k >= '0' && k <= '9') || k == '.')))) header_key = false;
      }
      if (header_key) {
        if (scan_header_suffix(lexer)) {
          array_delete(&text);
          lexer->result_symbol = KEY;
          return true;
        }
        lexer->mark_end(lexer);
        continue;
      }
    }
    // Expanded list markers are structural, not a cell or an unquoted key.
    if (text.size == 1 && text.contents[0] == '-' && c == ' ' && separator) {
      array_delete(&text);
      return false;
    }
    array_push(&text, c);
    advance(lexer);
    if (c != ' ') {
      trimmed = text.size;
      lexer->mark_end(lexer);
    }
    if (escaped) escaped = false;
    else if (c == '\\' && quoted) escaped = true;
    else if (c == '"') quoted = !quoted;
  }
  text.size = trimmed;
  if (valid[LIST_MARKER] && equals(&text, "-")) {
    array_delete(&text);
    lexer->result_symbol = LIST_MARKER;
    return true;
  }
  bool primitive = numeric(&text) || equals(&text, "true") || equals(&text, "false") || equals(&text, "null") || (!separator && equals(&text, "[]"));
  bool result = trimmed && !primitive && valid[symbol];
  array_delete(&text);
  if (result) lexer->result_symbol = symbol;
  return result;
}

bool tree_sitter_toon_external_scanner_scan(void *payload, TSLexer *lexer, const bool *valid) {
  Scanner *scanner = payload;
  // During error recovery every external token can be valid. Do not mutate scope.
  if (valid[INDENT] && valid[DEDENT] && valid[KEY] && valid[STRING] && valid[CELL_COMMA] && valid[CELL_PIPE]) return false;
  if (scanner->pending_dedents) {
    if (!valid[DEDENT]) return false;
    scanner->pending_dedents--;
    array_pop(&scanner->indents);
    lexer->result_symbol = DEDENT;
    return true;
  }
  if (scanner->pending_indent > *array_back(&scanner->indents)) {
    if (!valid[INDENT]) return false;
    array_push(&scanner->indents, scanner->pending_indent);
    scanner->pending_indent = 0;
    lexer->result_symbol = INDENT;
    return true;
  }
  scanner->pending_indent = 0;
  bool line_start = lexer->get_column(lexer) == 0;
  uint32_t indent = 0;
  bool tabs = false;
  if (!line_start) {
    while (lexer->lookahead == ' ') skip(lexer);
    if (lexer->lookahead == '\n' || lexer->lookahead == '\r') {
      if (!valid[NEWLINE]) return false;
      consume_newline(lexer, false);
      lexer->result_symbol = NEWLINE;
      scanner->eof_newline = false;
      return true;
    }
  } else {
    for (;;) {
      indent = 0;
      tabs = false;
      while (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
        if (lexer->lookahead == '\t') { tabs = true; indent = (indent + 2) & ~1u; }
        else indent++;
        skip(lexer);
      }
      if (!tabs && lexer->lookahead == '#' && valid[COMMENT]) {
        while (!lexer->eof(lexer) && lexer->lookahead != '\n' && lexer->lookahead != '\r') advance(lexer);
        consume_newline(lexer, false);
        lexer->result_symbol = COMMENT;
        return true;
      }
      if (lexer->lookahead != '\n' && lexer->lookahead != '\r') break;
      if (valid[BLANK_LINE]) {
        consume_newline(lexer, false);
        lexer->result_symbol = BLANK_LINE;
        return true;
      }
      consume_newline(lexer, true);
    }
  }
  if (lexer->eof(lexer)) {
    if (valid[NEWLINE] && !scanner->eof_newline) {
      scanner->eof_newline = true;
      lexer->result_symbol = NEWLINE;
      return true;
    }
    if (valid[DEDENT] && scanner->indents.size > 1) {
      array_pop(&scanner->indents);
      lexer->result_symbol = DEDENT;
      return true;
    }
    return false;
  }
  scanner->eof_newline = false;
  if (line_start) {
    uint32_t current = *array_back(&scanner->indents);
    if (indent > current && valid[INDENT]) {
      array_push(&scanner->indents, indent);
      lexer->result_symbol = INDENT;
      return true;
    }
    if (indent < current && valid[DEDENT]) {
      for (unsigned i = scanner->indents.size - 1; i > 0 && scanner->indents.contents[i] > indent; i--) scanner->pending_dedents++;
      scanner->pending_dedents--;
      scanner->pending_indent = indent;
      array_pop(&scanner->indents);
      lexer->result_symbol = DEDENT;
      return true;
    }
  }
  return scan_token(lexer, valid);
}
