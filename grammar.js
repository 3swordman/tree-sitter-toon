/**
 * @file TOON (Token-Oriented Object Notation) grammar for tree-sitter
 * @author 3swordman <yyuxiaoran@qq.com>
 * @license GPL-3.0-or-later
 * @see {@link https://github.com/toon-format/spec TOON Specification v2.0}
 */

/// <reference types="tree-sitter-cli/dsl" />
// @ts-check

module.exports = grammar({
  name: 'toon',

  externals: $ => [
    $._indent,
    $._dedent,
  ],

  conflicts: $ => [
    [$.pair],
    [$.array_value, $.value],
  ],

  extras: $ => [
    /[ \t]/,
  ],



  rules: {
    source_file: $ => seq(
      optional($._newline),
      optional($.document),
      optional($._newline)
    ),

    document: $ => choice(
      $.array,
      $.object,
      $.value
    ),

    // Objects - root level or nested
    object: $ => choice(
      // Root level object (at least one pair)
      prec.left(1, repeat1($.pair)),
      // Nested object (with indent/dedent)
      prec(2, seq($._indent, repeat1($.pair), $._dedent))
    ),

    pair: $ => seq(
      field('key', $.key),
      choice(
        // Pair with header (array)
        seq(
          $.header,
          token.immediate(':'),
          optional(/[ \t]+/),
          choice(
            // Inline array  
            seq(field('value', $.inline_values), $._newline),
            // Newline followed by indented array body
            seq($._newline, $._indent, field('value', $.array_body), $._dedent),
            // Empty array - just newline
            prec.dynamic(-1, $._newline)
          )
        ),
        // Pair without header - primitive value
        seq(
          token.immediate(':'),
          optional(/[ \t]+/),
          field('value', $.value),
          $._newline
        ),
        // Pair without header - nested object (may be empty)
        seq(
          token.immediate(':'),
          $._newline,
          optional(field('value', $.object))
        )
      )
    ),

    key: $ => choice(
      $.unquoted_key,
      $.string
    ),

    unquoted_key: $ => /[A-Za-z_][A-Za-z0-9_.]*/,

    // Arrays (root level)
    array: $ => seq(
      $.header,
      ':',
      optional(/[ \t]+/),
      choice(
        // Inline values
        seq($.inline_values, $._newline),
        // List/tabular body (indented at root)
        seq($._newline, $._indent, $.array_body, $._dedent),
        // Empty array
        prec.dynamic(-1, $._newline)
      )
    ),

    array_body: $ => repeat1(choice(
      prec(2, $.row),
      prec(1, $.tabular_row)
    )),

    header: $ => seq(
      '[',
      field('length', $.number),
      optional(field('delimiter', $.delimiter)),
      ']',
      optional(seq(
        token.immediate('{'),
        field('fields', $.field_list),
        '}'
      ))
    ),

    delimiter: $ => token.immediate(choice('\t', '|')),

    inline_values: $ => seq(
      alias($.array_value, $.value),
      repeat(seq(
        choice(',', '|', '\t'),
        optional(/[ \t]*/),
        alias($.array_value, $.value)
      ))
    ),

    // Values used in array contexts (inline arrays and list items)
    array_value: $ => choice(
      $.null,
      $.boolean,
      $.number,
      $.string,
      alias($.array_unquoted_string, $.unquoted_string)
    ),

    field_list: $ => seq(
      $.field_name,
      repeat(seq(
        choice(',', '|', '\t'),
        $.field_name
      ))
    ),

    field_name: $ => choice(
      $.unquoted_key,
      $.string
    ),

    row: $ => choice(
      prec(2, $.value_row),
      prec(1, $.object_row)
    ),

    value_row: $ => seq(
      token(seq('-', /[ \t]+/)),
      $.row_values,
      $._newline
    ),

    row_values: $ => choice(
      prec(2, $.delimited_values),
      prec(1, $.single_value)
    ),

    single_value: $ => alias($.array_value, $.value),

    delimited_values: $ => seq(
      alias($.array_value, $.value),
      repeat1(seq(
        choice(',', '|', '\t'),
        optional(/[ \t]*/),
        alias($.array_value, $.value)
      ))
    ),

    tabular_row: $ => prec(-1, seq(
      alias($.tabular_value, $.value),
      repeat(seq(
        choice(',', '|', '\t'),
        optional(/[ \t]*/),
        alias($.tabular_value, $.value)
      )),
      $._newline
    )),

    tabular_value: $ => choice(
      $.null,
      $.boolean,
      $.number,
      $.string,
      alias($.array_unquoted_string, $.unquoted_string)
    ),

    object_row: $ => choice(
      // Object with first field on hyphen line (simple value)
      prec(2, seq(
        token(seq('-', /[ \t]+/)),
        alias($.object_with_first_field, $.object)
      )),
      // Object with all fields indented (no field on hyphen line)
      prec(1, seq(
        '-',
        $._newline,
        $.object
      ))
    ),

    object_with_first_field: $ => prec.right(seq(
      alias($.first_field, $.pair),
      optional(seq(
        $._indent,
        repeat($.pair),
        $._dedent
      ))
    )),

    first_field: $ => prec.right(choice(
      // Field with header (array) followed by inline values
      prec(3, seq(
        field('key', $.key),
        $.header,
        ':',
        /[ \t]+/,
        field('value', $.inline_values),
        $._newline
      )),
      // Field with header (array) followed by indented body
      prec(2, seq(
        field('key', $.key),
        $.header,
        ':',
        $._newline,
        $._indent,
        field('value', $.array_body),
        $._dedent
      )),
      // Field with header (array) - empty array
      prec(1, seq(
        field('key', $.key),
        $.header,
        ':',
        $._newline
      )),
      // Field without header - nested object
      prec(2, seq(
        field('key', $.key),
        ':',
        $._newline,
        field('value', $.object)
      )),
      // Field without header - inline values (delimited primitives)
      prec(2, seq(
        field('key', $.key),
        ':',
        /[ \t]+/,
        field('value', $.inline_values),
        $._newline
      )),
      // Field without header - single primitive value
      prec(1, seq(
        field('key', $.key),
        ':',
        /[ \t]+/,
        field('value', $.value),
        $._newline
      ))
    )),

    // Values  
    value: $ => choice(
      $.string,
      $.null,
      $.boolean,
      $.number,
      $.unquoted_string
    ),

    // Unquoted strings: cannot start with -, and cannot contain : " [ ] { } \n \r or have leading/trailing space
    // Note: Delimiters (comma, pipe, tab) are allowed in unquoted strings for object values (no array header)
    // They are only special within array inline values/tabular rows (where splitting occurs)
    // This pattern explicitly excludes strings that would be valid numbers (those use the number rule)
    unquoted_string: $ => token(choice(
      // Starts with non-digit (but not -), anything after (excluding only structural chars, not delimiters)
      seq(/[^\s:"\[\]{}\n\r\-0-9]/, optional(/[^\n\r:"\[\]{}]+/), optional(/[^\s:"\[\]{}\n\r]/)),
      // Starts with "0" followed by digits (forbidden leading zeros - must be string)
      seq('0', /[0-9]/, optional(/[^\n\r:"\[\]{}]*/), optional(/[^\s:"\[\]{}\n\r]/)),
      // Numeric-like patterns that aren't valid numbers (e.g., "1.2.3", "16:9")
      // These contain colons or multiple dots that disqualify them as numbers
      seq(/[0-9]/, /[0-9.]*/, choice(
        seq(':', /[^\n\r"\[\]{}]*/),  // Contains colon (e.g., "16:9")
        seq('.', /[0-9]+/, '.', /[^\n\r"\[\]{}]*/)  // Multiple dots (e.g., "1.2.3")
      ), optional(/[^\s:"\[\]{}\n\r]/)),
      // Starts with digit, has non-number chars in middle/end  
      seq(/[0-9]/, /[^\n\r:"\[\]{}]*[^\s:"\[\]{}\n\r0-9eE+.\-]/, optional(/[^\n\r:"\[\]{}]*/), optional(/[^\s:"\[\]{}\n\r]/))
    )),

    // Array unquoted strings: used in inline arrays and tabular rows where delimiters are special
    // Must exclude comma, pipe, and tab since those are used to split values
    array_unquoted_string: $ => token(choice(
      // Starts with non-digit (but not -), anything after (excluding delimiters)
      seq(/[^\s:"\[\]{},|\t\n\r\-0-9]/, optional(/[^\n\r:"\[\]{},|\t]+/), optional(/[^\s:"\[\]{},|\t\n\r]/)),
      // Starts with "0" followed by digits (forbidden leading zeros - must be string)
      seq('0', /[0-9]/, optional(/[^\n\r:"\[\]{},|\t]*/), optional(/[^\s:"\[\]{},|\t\n\r]/)),
      // Starts with digit, has non-number chars in middle/end (excluding delimiters)
      seq(/[0-9]/, /[^\n\r:"\[\]{},|\t]*[^\s:"\[\]{},|\t\n\r0-9eE+.\-]/, optional(/[^\n\r:"\[\]{},|\t]*/), optional(/[^\s:"\[\]{},|\t\n\r]/))
    )),

    null: $ => 'null',

    boolean: $ => choice('true', 'false'),

    number: $ => token(seq(
      optional('-'),
      choice(
        '0',
        seq(/[1-9]/, repeat(/[0-9]/))
      ),
      optional(seq('.', repeat1(/[0-9]/))),
      optional(seq(
        /[eE]/,
        optional(/[+-]/),
        repeat1(/[0-9]/)
      ))
    )),

    string: $ => seq(
      '"',
      repeat(choice(
        token.immediate(prec(1, /[^"\\]+/)),
        $.escape_sequence
      )),
      '"'
    ),

    escape_sequence: $ => token.immediate(seq(
      '\\',
      choice(
        '"',
        '\\',
        'n',
        'r',
        't'
      )
    )),

    _newline: $ => '\n'
  }
});