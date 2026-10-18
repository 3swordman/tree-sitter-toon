#include "tree_sitter/parser.h"
#include "tree_sitter/array.h"
#include <string.h>

enum TokenType {
  INDENT, DEDENT, COMMENT, NEWLINE, KEY, STRING,
  CELL_COMMA, CELL_PIPE, CELL_TAB,
  FIRST_COMMA, FIRST_PIPE, FIRST_TAB, LIST_MARKER, BLANK_LINE, ENTRY_KEY,
  FIELD_COMMA, FIELD_PIPE, FIELD_TAB, OBJECT_START,
};

typedef struct {
  Array(uint32_t) indents;
  bool eof_newline;
  uint16_t pending_dedents;
  uint32_t pending_indent;
  uint32_t indent_size;
} Scanner;

static void advance(TSLexer *lexer) { lexer->advance(lexer, false); }
static void skip(TSLexer *lexer) { lexer->advance(lexer, true); }

void *tree_sitter_toon_external_scanner_create(void) {
  Scanner *scanner = calloc(1, sizeof(Scanner));
  array_init(&scanner->indents);
  array_push(&scanner->indents, 0);
  scanner->indent_size = 2;
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
  unsigned limit = (TREE_SITTER_SERIALIZATION_BUFFER_SIZE - 11) / sizeof(uint32_t);
  if (count > limit) count = limit;
  buffer[0] = scanner->eof_newline;
  memcpy(buffer + 1, &scanner->pending_dedents, 2);
  memcpy(buffer + 3, &scanner->pending_indent, 4);
  memcpy(buffer + 7, &scanner->indent_size, 4);
  memcpy(buffer + 11, scanner->indents.contents, count * sizeof(uint32_t));
  return 11 + count * sizeof(uint32_t);
}

void tree_sitter_toon_external_scanner_deserialize(void *payload, const char *buffer, unsigned length) {
  Scanner *scanner = payload;
  array_clear(&scanner->indents);
  scanner->eof_newline = length ? buffer[0] : false;
  scanner->pending_dedents = 0;
  if (length >= 3) memcpy(&scanner->pending_dedents, buffer + 1, 2);
  scanner->pending_indent = 0;
  if (length >= 7) memcpy(&scanner->pending_indent, buffer + 3, 4);
  scanner->indent_size = 2;
  if (length >= 11) memcpy(&scanner->indent_size, buffer + 7, 4);
  for (unsigned offset = 11; offset + sizeof(uint32_t) <= length; offset += sizeof(uint32_t)) {
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

// Header discovery uses the first unquoted bracket and colon, even when the
// bracket segment is malformed. Keep mark_end before '[' so the grammar can
// diagnose the header instead of swallowing it as a literal key.
static bool header_has_colon(TSLexer *lexer, TokenText *text) {
  bool quoted = false, escaped = false;
  while (!lexer->eof(lexer) && lexer->lookahead != '\n') {
    int32_t c = lexer->lookahead;
    if (!quoted && c == ':') return true;
    if (text) array_push(text, c);
    advance(lexer);
    if (escaped) escaped = false;
    else if (quoted && c == '\\') escaped = true;
    else if (c == '"') quoted = !quoted;
  }
  return false;
}

static bool scan_token(TSLexer *lexer, const bool *valid, bool leading_cr) {
  if (!leading_cr && (lexer->eof(lexer) || lexer->lookahead == '"' || lexer->lookahead == '\n')) return false;
  int symbol = STRING;
  int32_t separator = 0;
  bool first = false, field = false;
  for (int i = 0; i < 3; i++) {
    if (valid[FIELD_COMMA + i]) {
      field = true;
      symbol = FIELD_COMMA + i;
      separator = i == 0 ? ',' : i == 1 ? '|' : '\t';
      break;
    }
  }
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
  bool key_candidate = !field && (valid[KEY] || valid[ENTRY_KEY]);
  if (leading_cr) {
    array_push(&text, '\r');
    trimmed = 1;
    lexer->mark_end(lexer);
  }
  while (!lexer->eof(lexer) && lexer->lookahead != '\n') {
    int32_t c = lexer->lookahead;
    if (!quoted && field && (c == '{' || c == '}' || c == ',' || c == '|' || c == '\t')) break;
    if (!quoted && separator && c == separator) break;
    if (c == '\r') {
      advance(lexer);
      // Only a single CR at the very end of a line is stripped (§12).
      if (lexer->lookahead == '\n' || lexer->eof(lexer)) break;
      array_push(&text, c);
      trimmed = text.size;
      lexer->mark_end(lexer);
      continue;
    }
    if (valid[LIST_MARKER] && text.size == 1 && text.contents[0] == '-' && c == ' ') {
      while (lexer->lookahead == ' ') advance(lexer);
      lexer->mark_end(lexer);
      array_delete(&text);
      lexer->result_symbol = LIST_MARKER;
      return true;
    }
    if (!quoted && c == ':' && (key_candidate || first)) {
      if (!separator && !valid[ENTRY_KEY] && text.size && text.contents[0] == '[') {
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
    if (!quoted && c == '[' && !field && valid[KEY]) {
      bool has_key = text.size > 0;
      bool tab_before_bracket = has_key && text.contents[text.size - 1] == '\t';
      bool colon = header_has_colon(lexer, &text);
      if (colon) {
        array_delete(&text);
        if (!has_key || tab_before_bracket) return false;
        lexer->result_symbol = KEY;
        return true;
      }
      // No unquoted colon means this is a scalar, including items[2].
      while (text.size && (text.contents[text.size - 1] == ' ' || text.contents[text.size - 1] == '\r')) text.size--;
      bool empty_array = equals(&text, "[]");
      array_delete(&text);
      if (!valid[STRING] || empty_array) return false;
      lexer->mark_end(lexer);
      lexer->result_symbol = STRING;
      return true;
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
  bool primitive = !field && (numeric(&text) || equals(&text, "true") || equals(&text, "false") || equals(&text, "null") || (!separator && equals(&text, "[]")));
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
    (void)array_pop(&scanner->indents);
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
  bool tabs = false, leading_cr = false;
  if (!line_start) {
    while (lexer->lookahead == ' ') skip(lexer);
    bool carriage_return = lexer->lookahead == '\r';
    if (lexer->lookahead == '\r') {
      advance(lexer);
      if (lexer->lookahead != '\n' && !lexer->eof(lexer)) leading_cr = true;
    }
    if (!leading_cr && (lexer->lookahead == '\n' || (carriage_return && lexer->eof(lexer)))) {
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
        if (lexer->lookahead == '\t') { tabs = true; indent += 2; }
        else indent++;
        skip(lexer);
      }
      if (!tabs && lexer->lookahead == '#' && valid[COMMENT]) {
        while (!lexer->eof(lexer) && lexer->lookahead != '\n') advance(lexer);
        consume_newline(lexer, false);
        lexer->result_symbol = COMMENT;
        return true;
      }
      // Keep exact widths for configurable indentation. For editor recovery,
      // align a nearby existing scope in the same default two-space bucket.
      if (scanner->indent_size != 1 && indent > 1) {
        for (unsigned i = scanner->indents.size; i > 0; i--) {
          uint32_t known = scanner->indents.contents[i - 1];
          if (known > 1 && known / 2 == indent / 2) { indent = known; break; }
        }
      }
      if (lexer->lookahead == '\r') {
        lexer->mark_end(lexer);
        advance(lexer);
        if (lexer->lookahead != '\n' && !lexer->eof(lexer)) { leading_cr = true; break; }
      }
      if (lexer->lookahead != '\n') break;
      if (valid[BLANK_LINE]) {
        consume_newline(lexer, false);
        lexer->mark_end(lexer);
        lexer->result_symbol = BLANK_LINE;
        return true;
      }
      consume_newline(lexer, true);
    }
  }
  if (!leading_cr && lexer->eof(lexer)) {
    lexer->mark_end(lexer);
    if (valid[NEWLINE] && !scanner->eof_newline) {
      scanner->eof_newline = true;
      lexer->result_symbol = NEWLINE;
      return true;
    }
    if (valid[DEDENT] && scanner->indents.size > 1) {
      (void)array_pop(&scanner->indents);
      lexer->result_symbol = DEDENT;
      return true;
    }
    return false;
  }
  scanner->eof_newline = false;
  if (line_start) {
    uint32_t current = *array_back(&scanner->indents);
    if (indent > current && valid[INDENT]) {
      if (scanner->indents.size == 1) scanner->indent_size = indent;
      array_push(&scanner->indents, indent);
      lexer->result_symbol = INDENT;
      return true;
    }
    if (indent < current && valid[DEDENT]) {
      for (unsigned i = scanner->indents.size - 1; i > 0 && scanner->indents.contents[i] > indent; i--) scanner->pending_dedents++;
      scanner->pending_dedents--;
      scanner->pending_indent = indent;
      (void)array_pop(&scanner->indents);
      lexer->result_symbol = DEDENT;
      return true;
    }
    if (indent != current) return false;
  }
  if (valid[OBJECT_START]) {
    lexer->mark_end(lexer);
    // Keyless array headers stand at the hyphen's physical depth. All other
    // items share a virtual field depth, so an empty first field cannot capture
    // its siblings and a primitive cannot open a nested scope.
    if (lexer->lookahead == '[' && header_has_colon(lexer, NULL)) return false;
    uint32_t field_indent = *array_back(&scanner->indents) + scanner->indent_size;
    array_push(&scanner->indents, field_indent);
    lexer->result_symbol = OBJECT_START;
    return true;
  }
  return scan_token(lexer, valid, leading_cr);
}
