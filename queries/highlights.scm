(comment) @comment
(string) @string
(unquoted_string) @string
(escape_sequence) @string.escape
(number) @number
(boolean) @boolean
(null) @constant.builtin

(key (unquoted_key) @property)
(key (string) @property)
(field_name (unquoted_key) @property)
(field_name (string) @property)

["[" "]" "{" "}"] @punctuation.bracket
[":" "," "|" "\t"] @punctuation.delimiter
(delimiter) @punctuation.delimiter
(empty_array) @punctuation.bracket
"-" @punctuation.list_marker
