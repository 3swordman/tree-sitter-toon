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
  ],

  extras: $ => [
    /[ \t]/,
  ],



  rules: {
    source_file: $ => choice(
      seq(optional($._newline), $.document, optional($._newline)),
      repeat1($._newline)
    ),

    document: $ => choice(
      $.object,
      $.array,
      $.value
    ),

    // Objects - root level or nested
    object: $ => choice(
      // Root level object (no indent/dedent)
      prec.left(1, repeat1($.pair)),
      // Nested object (with indent/dedent)
      seq($._indent, repeat1($.pair), $._dedent)
    ),

    pair: $ => choice(
      // Pair with header (array)
      seq(
        field('key', $.key),
        $.header,
        ':',
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
      // Pair without header  
      seq(
        field('key', $.key),
        ':',
        optional(/[ \t]+/),
        field('value', choice(
          prec(2, seq($.inline_values, $._newline)),
          prec(1, seq($.value, $._newline)),
          seq($._newline, $.object)
        ))
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
      $.value,
      repeat(seq(
        choice(',', '|', '\t'),
        optional(/[ \t]*/),
        $.value
      ))
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
      $.single_value,
      $.delimited_values
    ),

    single_value: $ => $.value,

    delimited_values: $ => seq(
      $.value,
      repeat1(seq(
        choice(',', '|', '\t'),
        optional(/[ \t]*/),
        $.value
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
      $.unquoted_string
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

    object_with_first_field: $ => prec.left(seq(
      // First field on the hyphen line
      alias($.first_field, $.pair),
      // Remaining fields indented
      optional(seq($._indent, repeat($.pair), $._dedent))
    )),

    first_field: $ => choice(
      // Field with header (array)
      seq(
        field('key', $.key),
        $.header,
        ':',
        optional(/[ \t]+/),
        field('value', choice(
          // Inline array
          seq($.inline_values, $._newline),
          // List/tabular body would need to be indented further (not on hyphen line)
          // Empty array
          $._newline
        ))
      ),
      // Field without header
      seq(
        field('key', $.key),
        ':',
        optional(/[ \t]+/),
        field('value', choice(
          prec(2, seq($.inline_values, $._newline)),
          prec(1, seq($.value, $._newline))
        ))
      )
    ),

    // Values
    value: $ => choice(
      $.null,
      $.boolean,
      $.number,
      $.string,
      $.unquoted_string
    ),

    // Unquoted strings: cannot start with -, and cannot contain : " [ ] { } , | \t \n \r or have leading/trailing space
    // This matches the TOON spec: strings that don't need quotes (except for the restricted cases)
    unquoted_string: $ => token(prec(-1, /[^\s:"\[\]{},|\t\n\r-][^\n\r:"\[\]{},|\t]*[^\s:"\[\]{},|\t\n\r]|[^\s:"\[\]{},|\t\n\r-]/)),

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