"""Check TOON 4.4 fixture bytes and syntax-tree behavior with the Tree-sitter CLI.

This checks syntax acceptance and selected tree properties, not decoder semantics
such as array counts, duplicate-key handling, or numeric overflow policies.
"""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as ET

from cst_value import FixtureTree

ROOT = Path(__file__).resolve().parents[1]
BUNDLED_CLI = ROOT / "node_modules/tree-sitter-cli" / ("tree-sitter.exe" if os.name == "nt" else "tree-sitter")
CLI = str(BUNDLED_CLI) if BUNDLED_CLI.is_file() else shutil.which("tree-sitter") or "tree-sitter"


class V4SyntaxTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.input = Path(self.directory.name) / "input.toon"

    def parse(self, source, edits=()):
        self.input.write_bytes(source.encode("utf-8"))
        command = [CLI, "parse", "--xml", str(self.input)]
        if edits:
            command.extend(["--edits", *edits])
        result = subprocess.run(command, cwd=ROOT, text=True, capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return ET.fromstring(result.stdout)

    def test_exact_v4_fixture_inputs(self):
        for kind in ("decode", "encode"):
            fixtures = json.loads((ROOT / f"test/fixtures/v4-{kind}.json").read_text())
            self.assertEqual(fixtures["version"], "4.4")
            for case in fixtures["tests"]:
                with self.subTest(kind=kind, category=case["category"], name=case["name"]):
                    source = case["input"] if kind == "decode" else case["expected"]
                    tree = self.parse(source)
                    document = tree.find("document")
                    actual = {} if document is None else FixtureTree(source, self.node_source).value(
                        next(child for child in document if child.tag != "comment"))
                    self.assertEqual(actual, case["expected"] if kind == "decode" else case["input"])

    def reject(self, source):
        self.input.write_bytes(source.encode("utf-8"))
        result = subprocess.run([CLI, "parse", "--quiet", str(self.input)],
                                cwd=ROOT, text=True, capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)

    @staticmethod
    def node_source(source, node):
        # Tree-sitter's ranges are byte columns. XML normalizes literal CRs and
        # omits anonymous string content, so inspect the original bytes instead.
        # Only LF is a line separator in TOON, including when controls are data.
        lines = source.encode("utf-8").split(b"\n")
        start = sum(len(line) + 1 for line in lines[:int(node.get("srow"))]) + int(node.get("scol"))
        end = sum(len(line) + 1 for line in lines[:int(node.get("erow"))]) + int(node.get("ecol"))
        return source.encode("utf-8")[start:end].decode("utf-8")

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
        tree = self.parse('m[3:]{v}:\n  [2]: 1\n  [oops: 2\n  - key: 3')
        self.assertEqual([node.text for node in tree.findall(".//keyed_row/key/unquoted_key")],
                         ['[2]', '[oops', '- key'])

    def test_primitive_classification(self):
        for value, kind in [("-05", "unquoted_string"), (".5", "unquoted_string"), ("1.", "unquoted_string"), ("+5", "unquoted_string"), ("1e3", "number"), ("-0.5", "number"), ("true", "boolean"), ("null", "null"), ("[]", "empty_array")]:
            with self.subTest(value=value):
                tree = self.parse(f"key: {value}")
                self.assertEqual(tree.find(".//pair/value")[0].tag, kind)

    def test_literal_keys_and_header_keys(self):
        for key in ["foo-bar", "2key", "a.b", 'a"b:c"d', "n\u00a0", "世界"]:
            with self.subTest(key=key):
                tree = self.parse(f"{key}: 1")
                self.assertEqual(tree.find(".//pair/key/unquoted_key").text, key)
                self.assertIsNone(tree.find(".//header"))
                tree = self.parse(f"{key}[1]: x")
                self.assertEqual(tree.find(".//pair/key/unquoted_key").text, key)
                self.assertIsNotNone(tree.find(".//header"))

    def test_field_names_and_nested_groups(self):
        for marker, separator in [("", ","), ("|", "|"), ("\t", "\t")]:
            source = f'a[1{marker}]{{2key{separator}x:y{separator}b\u00a0{{n}}{separator}x"}}"y}}:\n  1{separator}2{separator}3{separator}4'
            tree = self.parse(source)
            self.assertEqual([node.text for node in tree.findall(".//field_name/unquoted_key")],
                             ["2key", "x:y", "b\u00a0", "n", 'x"}"y'])

    def test_empty_keys_and_spaces_before_colons(self):
        tree = self.parse(': 1\na :\n  : 2\nitems[1]:\n  - : 3\nm[1:]{v}:\n  : 4\n"quoted" : 5')
        self.assertEqual(len(tree.findall(".//pair")), 7)
        self.assertEqual(len([node for node in tree.findall(".//pair") if node.find("key") is None]), 3)
        self.assertIsNone(tree.find(".//keyed_row/key"))
        self.assertEqual(len(tree.findall(".//keyed_row/inline_values/value")), 1)

    def test_hyphens_are_structural_only_at_item_depth(self):
        tree = self.parse('items[1]:\n  - a: 1\n    - y: 2\nrows[1]{a,b}:\n  - y,2')
        self.assertEqual(tree.find(".//object_row/object/pair[2]/key/unquoted_key").text, "- y")
        self.assertEqual(tree.find(".//tabular_row/value/unquoted_string").text, "- y")

    def test_empty_objects_do_not_capture_sibling_fields(self):
        tree = self.parse('empty:\na: 1\nnested:\n  empty:\n  b: 2\nitems[1]:\n  - empty:\n    c: 3')
        self.assertEqual(len(tree.findall("./document/object/pair")), 4)
        self.assertEqual(len(tree.findall(".//pair/object/pair")), 2)
        self.assertEqual(len(tree.findall(".//object_row/object/pair")), 2)
        for pair in tree.findall(".//pair"):
            key = pair.find("key/unquoted_key")
            if key is not None and key.text == "empty":
                self.assertIsNone(pair.find("object"))

    def test_custom_indentation_preserves_list_field_scopes(self):
        for width in (1, 2, 3, 4, 5, 8):
            with self.subTest(width=width):
                unit = " " * width
                source = (f"items[1]:\n{unit}- empty:\n{unit * 2}table[1]{{x}}:\n"
                          f"{unit * 3}1\n{unit * 2}nested:\n{unit * 3}y: 2\nend: 3")
                tree = self.parse(source)
                actual = FixtureTree(source, self.node_source).value(tree.find("document/object"))
                self.assertEqual(actual, {"items": [{"empty": {}, "table": [{"x": 1}],
                                                      "nested": {"y": 2}}], "end": 3})

    def test_raw_controls_and_carriage_return_preparation(self):
        for codepoint in range(32):
            if codepoint == 10:
                continue
            with self.subTest(codepoint=codepoint):
                source = f'val: "x{chr(codepoint)}y"'
                tree = self.parse(source)
                self.assertEqual(self.node_source(source, tree.find(".//string")), source[5:])
        for value in ["x\r", "\rx", "x\ry"]:
            for source in [f"a: {value}\r\nb: 1", f"a[1]{{v}}:\n  {value}\r\n"]:
                tree = self.parse(source)
                self.assertEqual(self.node_source(source, tree.find(".//value/unquoted_string")), value)

    def test_byte_order_mark_only_at_document_start(self):
        source = '\ufeff\ufeffa: 1\n\ufeffb: 2'
        tree = self.parse(source)
        self.assertEqual([node.text for node in tree.findall(".//key/unquoted_key")], ['\ufeffa', '\ufeffb'])
        tree = self.parse('\ufeff# comment\na: 1')
        self.assertEqual(len(tree.findall(".//comment")), 1)

    def test_malformed_syntax_is_rejected(self):
        cases = [
            "foo[03]: 1", "foo[-1]: 1", "foo[1][bar]: 1", "key[]: 1",
            "foo[2]extra: a,b", "a[1:", "a[2:]{x}", "foo [2]: a,b",
            "t\t[1]: x", '"t" [1]: x', "[2] : a,b", "a[2:]:",
            "a[2|:]{x}:", "a[2 :]{x}:", "a[2:,]{x}:",
            "a[0]{}:", "a[0]{x,}:", "a[0]{x,{y}}:", "a[0]{b {c}}:",
            "a[0]{b{c}d}:", "a[0]{b{}}:", "a[0]{b{c}:",
            "a[0|]{x,y}:", "a[0]{x|y}:", "a[0|]{x\ty}:",
            'a[0]{"x"y}:', 'a: "x"y', '"x"y: 1', '"x"\t: 1',
            'a[2]: "x"y,z', 'a: "x"\t', 'a: "unterminated',
            'a: "\\x"', 'a: "\\u123"', 'a: "\\ud800"', 'a: "\\ud83d\\ude80"',
            'a[1]{x}: 1', 'a[1:]{x}:\t\n  k: 1',
            'a:\n  [2]: x,y', 'items[1]:\n  - [0]{x}:',
            'a[1:]{x}:\n  no colon', 'a[1]:\n  bare',
            '[1]: x\njunk: 1', '[]\njunk: 1', 'hello\nworld',
            'a: 1\n  b: 2', 'a[1:]{x}:\n  k: 1\n    deeper: 2',
        ]
        for source in cases:
            with self.subTest(source=source):
                self.reject(source)

    def test_brackets_without_unquoted_colons_are_scalars(self):
        for source in ['items[2]', '[2]', 'k"x[2]: 1,2']:
            tree = self.parse(source)
            self.assertEqual(tree.find("./document/value/unquoted_string").text, source)
        tree = self.parse('items[1]:\n  - [2]')
        self.assertEqual(tree.find(".//single_value/value/unquoted_string").text, '[2]')

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

    def test_incremental_v44_tokens_and_virtual_item_scopes(self):
        cases = [
            ("empty:\na: 1", "empty:\n  a: 1"),
            ("items[1]:\n  - empty:\n    x: 1", "items[1]:\n  - empty:\n      x: 1"),
            ("items[1]:\n  - empty:\n    x: 1", "items[1]:\n  - empty:\n    : 1"),
            ("foo-bar[1]: x", "foo-bar: x"),
            ("items[1]: x", "items[1]"),
            ('a[1]{foo-bar}:\n  - y', 'a[1]{foo-bar{n}}:\n  - y'),
            ("a: x\r\r\nb: 1", "a: x\r\nb: 1"),
            ('val: "x\\u0001y"', 'val: "x\x01y"'),
        ]
        for source, target in cases:
            with self.subTest(source=source, target=target):
                before, after = source.encode("utf-8"), target.encode("utf-8")
                start = 0
                while start < min(len(before), len(after)) and before[start] == after[start]:
                    start += 1
                suffix = 0
                while (suffix < min(len(before), len(after)) - start
                       and before[-suffix - 1] == after[-suffix - 1]):
                    suffix += 1
                insertion = after[start:len(after) - suffix].decode("utf-8")
                edits = [f"{start} {len(before) - start - suffix} {insertion}"]
                self.assertEqual(self.shape(self.parse(source, edits)), self.shape(self.parse(target)))


if __name__ == "__main__":
    unittest.main()
