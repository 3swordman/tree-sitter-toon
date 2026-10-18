/**
 * @file TOON (Token-Oriented Object Notation) grammar for tree-sitter
 * @author 3swordman <yyuxiaoran@qq.com>
 * @license MIT
 * @see {@link https://github.com/toon-format/spec/blob/v4.4.0/SPEC.md TOON Specification v4.4}
 */

/// <reference types="tree-sitter-cli/dsl" />
// @ts-check

const delimiters = { comma: ",", pipe: "|", tab: "\t" };
const variants = (prefix) => Object.keys(delimiters).map((name) => `_${prefix}_${name}`);
const choose = ($, prefix) => choice(...variants(prefix).map((name) => $[name]));
const scopedRules = {};

for (const [name, separator] of Object.entries(delimiters)) {
  const rule = (prefix) => `_${prefix}_${name}`;
  const fields = ($) => alias($[rule("fields")], $.field_list);
  const cell = ($) => alias($[rule("cell")], $.value);
  const firstCell = ($) => alias($[rule("first_cell")], $.value);
  const header = ($) => alias($[rule("header")], $.header);
  const tableHeader = ($) => alias($[rule("table_header")], $.header);
  const keyedHeader = ($) => alias($[rule("keyed_header")], $.keyed_header);
  const body = ($) => alias($[rule("body")], $.array_body);
  const inline = ($) => alias($[rule("inline")], $.inline_values);

  // Each header selects its own delimiter; a missing marker means comma.
  scopedRules[rule("header")] = ($) => seq(
    token.immediate("["), field("length", alias($.length, $.number)),
    ...(name === "comma" ? [] : [field("delimiter", alias(token.immediate(separator), $.delimiter))]),
    token.immediate("]"),
  );
  scopedRules[rule("table_header")] = ($) => seq(
    $[rule("header")], token.immediate("{"), field("fields", fields($)), "}",
  );
  scopedRules[rule("keyed_header")] = ($) => seq(
    token.immediate("["), field("length", alias($.length, $.number)), token.immediate(":"),
    ...(name === "comma" ? [] : [field("delimiter", alias(token.immediate(separator), $.delimiter))]),
    token.immediate("]"), token.immediate("{"), field("fields", fields($)), "}",
  );
  scopedRules[rule("fields")] = ($) => seq(
    alias($[rule("field")], $.field_name),
    repeat(seq(separator, alias($[rule("field")], $.field_name))),
  );
  scopedRules[rule("field")] = ($) => seq(
    choice(alias($[rule("field_key")], $.unquoted_key), $.string),
    optional(seq(token.immediate("{"), field("fields", fields($)), "}")),
  );
  scopedRules[rule("cell")] = ($) => choice(
    $.null, $.boolean, $.number, $.string,
    alias($[rule("cell_string")], $.unquoted_string),
  );
  scopedRules[rule("first_cell")] = ($) => choice(
    $.null, $.boolean, $.number, $.string,
    alias($[rule("first_cell_string")], $.unquoted_string),
  );
  // Empty cells have no value node; separators still preserve their positions.
  scopedRules[rule("inline")] = ($) => choice(
    seq(cell($), repeat(seq(separator, optional(cell($))))),
    seq(separator, optional(cell($)), repeat(seq(separator, optional(cell($))))),
  );
  scopedRules[rule("table_row")] = ($) => seq(
    choice(
      seq(firstCell($), repeat(seq(separator, optional(cell($))))),
      seq(separator, optional(cell($)), repeat(seq(separator, optional(cell($))))),
    ), $._newline,
  );
  scopedRules[rule("keyed_row")] = ($) => seq(
    optional(field("key", alias($._entry_key_rule, $.key))), ":", optional(field("values", inline($))), $._newline,
  );
  scopedRules[rule("keyed_body")] = ($) => repeat1(alias($[rule("keyed_row")], $.keyed_row));
  scopedRules[rule("body")] = ($) => repeat1($.row);
  scopedRules[rule("list_content")] = ($) => prec.right(
    seq(header($), token.immediate(":"), choice(
      seq(field("value", inline($)), $._newline),
      seq($._newline, optional(seq($._indent, field("value", body($)), $._dedent))),
    )),
  );
  scopedRules[rule("array_content")] = ($) => prec.right(choice(
    $[rule("list_content")],
    seq(tableHeader($), token.immediate(":"), $._newline,
      optional(seq($._indent, field("value", alias(repeat1(alias($[rule("table_row")], $.tabular_row)), $.array_body)), $._dedent))),
  ));
  scopedRules[rule("keyed_content")] = ($) => prec.right(seq(
    keyedHeader($), token.immediate(":"), $._newline,
    optional(seq($._indent, field("value", alias($[rule("keyed_body")], $.keyed_body)), $._dedent)),
  ));
}

module.exports = grammar({
  name: "toon",
  externals: ($) => [
    $._indent, $._dedent, $.comment, $._newline,
    $.unquoted_key, $.unquoted_string,
    ...variants("cell_string").map((name) => $[name]),
    ...variants("first_cell_string").map((name) => $[name]), $._list_marker, $._blank_line, $._entry_key,
    ...variants("field_key").map((name) => $[name]),
    $._object_start,
  ],
  extras: ($) => [/ /, $.comment, $._blank_line],
  conflicts: ($) => [],
  rules: {
    source_file: ($) => seq(repeat($._newline), optional(seq($.document, repeat($._newline)))),
    document: ($) => choice($.array, $.keyed_object, alias($._root_object, $.object), $.value),
    _root_object: ($) => repeat1($.pair),
    object: ($) => seq($._indent, repeat1($.pair), $._dedent),
    pair: ($) => choice(seq(field("key", $.key), $._pair_content), $._value_content),
    _pair_content: ($) => choice(
      choose($, "array_content"), choose($, "keyed_content"), $._value_content,
    ),
    _value_content: ($) => choice(
      seq(":", field("value", $.value), $._newline),
      prec.right(seq(":", $._newline, optional(field("value", $.object)))),
    ),
    key: ($) => choice($.unquoted_key, $.string),
    _entry_key_rule: ($) => choice(alias($._entry_key, $.unquoted_key), $.string),
    array: ($) => choose($, "array_content"),
    keyed_object: ($) => choose($, "keyed_content"),
    list_array: ($) => choose($, "list_content"),
    row: ($) => choice(prec(2, $.object_row), prec(1, $.value_row)),
    value_row: ($) => seq(alias($._list_marker, "-"), choice(
      $.list_array, seq($._object_start, $.row_values, $._newline, $._dedent),
    )),
    row_values: ($) => $.single_value,
    single_value: ($) => $.value,
    object_row: ($) => choice(
      seq(alias($._list_marker, "-"), alias($.object_with_first_field, $.object)),
      prec.right(seq(alias($._list_marker, "-"), $._newline, optional($.object))),
    ),
    object_with_first_field: ($) => prec.right(seq(
      $._object_start, alias($.first_field, $.pair), repeat($.pair), $._dedent,
    )),
    first_field: ($) => choice(seq(field("key", $.key), $._pair_content), $._value_content),
    value: ($) => choice($.string, $.null, $.boolean, $.number, $.empty_array, $.unquoted_string),
    empty_array: ($) => "[]",
    length: ($) => token.immediate(choice("0", /[1-9][0-9]*/)),
    null: ($) => "null",
    boolean: ($) => choice("true", "false"),
    number: ($) => token(/-?(0|[1-9][0-9]*)(\.[0-9]+)?([eE][+-]?[0-9]+)?/),
    string: ($) => seq('"', repeat(choice(token.immediate(prec(1, /[^"\\\n]+/)), token.immediate("\0"), $.escape_sequence)), token.immediate('"')),
    escape_sequence: ($) => token.immediate(seq("\\", choice('"', "\\", "n", "r", "t", /u([0-9A-Ca-cE-Fe-f][0-9A-Fa-f]{3}|[dD][0-7][0-9A-Fa-f]{2})/))),
    ...scopedRules,
  },
});
