"""Check exact v4 fixture bytes and syntax-tree behavior with the Tree-sitter CLI.

This checks syntax acceptance and selected tree properties, not decoder semantics
such as array counts, duplicate-key handling, or numeric overflow policies.
"""
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]


class V4SyntaxTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.input = Path(self.directory.name) / "input.toon"

    def parse(self, source, edits=()):
        self.input.write_bytes(source.encode("utf-8"))
        command = ["tree-sitter", "parse", "--xml", str(self.input)]
        if edits:
            command.extend(["--edits", *edits])
        result = subprocess.run(command, cwd=ROOT, text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return ET.fromstring(result.stdout)

    def test_exact_v4_fixture_inputs(self):
        fixtures = json.loads((ROOT / "test/fixtures/v4-decode.json").read_text())
        for case in fixtures["tests"]:
            with self.subTest(category=case["category"], name=case["name"]):
                self.parse(case["input"])

    def test_delimiter_scope_and_literal_delimiters(self):
        tree = self.parse("items[2|]:\n  - inner[2]: a,b\n    text: c,d\n  - end: e,f\n")
        self.assertEqual([node.text for node in tree.findall(".//inline_values/value/unquoted_string")], ["a", "b"])
        self.assertEqual([node.text for node in tree.findall(".//pair/value/unquoted_string")], ["c,d", "e,f"])
        for marker, source, values in [
            ("|", "a,b|c", ["a,b", "c"]),
            ("\t", "a,b\tc|d", ["a,b", "c|d"]),
            ("", "a|b,c\td", ["a|b", "c\td"]),
        ]:
            with self.subTest(marker=marker):
                tree = self.parse(f"data[2{marker}]: {source}")
                self.assertEqual([node.text for node in tree.findall(".//inline_values/value/unquoted_string")], values)

    def test_keyed_entries_and_nested_fields(self):
        tree = self.parse('m[2:|]{name|meta{x|y}}:\n  "a:b": A,B|1|2\n  k[2]: C|3|4')
        self.assertEqual(len(tree.findall(".//keyed_header")), 1)
        self.assertEqual(len(tree.findall(".//keyed_row")), 2)
        self.assertEqual(tree.find(".//keyed_row/key/unquoted_key").text, "k[2]")
        self.assertEqual([node.text for node in tree.findall(".//field_name/field_list/field_name/unquoted_key")], ["x", "y"])
        self.assertEqual(tree.find(".//keyed_row/inline_values/value/unquoted_string").text, "A,B")

    def test_primitive_classification(self):
        for value, kind in [("-05", "unquoted_string"), (".5", "unquoted_string"), ("1.", "unquoted_string"), ("+5", "unquoted_string"), ("1e3", "number"), ("-0.5", "number"), ("true", "boolean"), ("null", "null"), ("[]", "empty_array")]:
            with self.subTest(value=value):
                tree = self.parse(f"key: {value}")
                self.assertEqual(tree.find(".//pair/value")[0].tag, kind)

    def test_literal_keys_and_malformed_headers(self):
        for key in ["foo-bar", "2key", "a.b", "foo[03]", "foo[-1]", "foo[1][bar]", "key[]", "foo[2]extra"]:
            with self.subTest(key=key):
                tree = self.parse(f"{key}: 1")
                self.assertEqual(tree.find(".//pair/key/unquoted_key").text, key)
                self.assertIsNone(tree.find(".//header"))

    def test_comments_crlf_and_eof_preserve_structure(self):
        source = "items[1]:\n  - table[1]{name,meta{x}}:\n      Ada,1\n    active: true\n"
        expected = self.shape(self.parse(source))
        for ending in ["\n", "\r\n"]:
            with self.subTest(ending=repr(ending)):
                commented = "# before\n" + source.replace("\n", "\n   # arbitrary indentation\n") + " # trailing comment without newline"
                self.assertEqual(self.shape(self.parse(commented.replace("\n", ending))), expected)

    @staticmethod
    def shape(node):
        # Ranges and layout change with comments and line endings; the data tree
        # must retain node types, fields, and complete leaf tokens.
        return (node.tag, node.get("field"), node.text if not len(node) else None,
                tuple(V4SyntaxTests.shape(child) for child in node if child.tag != "comment"))

    def test_incremental_header_and_comment_edits(self):
        source = "items[2|]: a,b|c\n"
        edits = [f"{source.index('|')} 1 \t", f"{source.rindex('|')} 1 \t"]
        target = source.replace("|", "\t")
        self.assertEqual(self.shape(self.parse(source, edits)), self.shape(self.parse(target)))
        source = "outer:\n  a: 1\n  b: 2\n"
        insertion = "# comment\n"
        self.assertEqual(self.shape(self.parse(source, [f"{source.index('  b:')} 0 {insertion}"])), self.shape(self.parse(source.replace("  b: 2", insertion + "  b: 2"))))

    def test_incremental_scope_changes_and_eof(self):
        cases = [
            ("a: 1", "a: 1\nb: 2", ["4 0 \nb: 2"]),
            ("outer:\n  a: 1", "outer:\n  a: 1\n  b: 2", ["13 0 \n  b: 2"]),
            ("[1]{v}:\n  1", "[1:]{v}:\n  a: 1", ["8 3   a: 1", "2 0 :"]),
        ]
        for source, target, edits in cases:
            with self.subTest(source=source):
                self.assertEqual(self.shape(self.parse(source, edits)), self.shape(self.parse(target)))
        source = "outer:\n  inner:\n    a: 1\n  b: 2\nend: 3"
        removed = "  inner:\n    a: 1\n"
        start = source.index(removed)
        replacement = "  x: 1\n"
        self.assertEqual(self.shape(self.parse(source, [f"{start} {len(removed)} {replacement}"])),
                         self.shape(self.parse(source.replace(removed, replacement))))


if __name__ == "__main__":
    unittest.main()
