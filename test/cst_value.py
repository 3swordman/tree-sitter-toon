"""Reconstruct fixture data from the public CST to check node boundaries/scopes.

This is a test oracle for valid input, not a decoder or validation API. Counts,
width errors, duplicate-key rejection and decoder options are deliberately absent.
"""
import json
import re


class FixtureTree:
    def __init__(self, source, node_source):
        self.source = source
        self.node_source = node_source

    def text(self, node):
        return self.node_source(self.source, node)

    def key(self, node):
        if node is None:
            return ""
        return self.primitive(next(child for child in node if child.tag in ("string", "unquoted_key")))

    def primitive(self, node):
        raw = self.text(node)
        if node.tag == "empty_array":
            return []
        if node.tag in ("string", "number", "boolean", "null"):
            return json.loads(raw, strict=False)
        return raw

    def cells(self, node, delimiter):
        if node is None:
            return []
        raw = self.text(node).split("\n")[0].removesuffix("\r")
        parts = []
        start = 0
        quoted = escaped = False
        for index, char in enumerate(raw):
            if not quoted and char == delimiter:
                parts.append(raw[start:index])
                start = index + 1
            if escaped:
                escaped = False
            elif quoted and char == "\\":
                escaped = True
            elif char == '"':
                quoted = not quoted
        parts.append(raw[start:])
        values = []
        for part in parts:
            part = part.strip(" ")
            if (part.startswith('"') or part in ("true", "false", "null")
                    or re.fullmatch(r"-?(0|[1-9][0-9]*)(\.[0-9]+)?([eE][+-]?[0-9]+)?", part)):
                values.append(json.loads(part, strict=False))
            else:
                values.append(part)
        return values

    def fields(self, node, cells):
        result = {}
        for field in node.findall("field_name"):
            nested = field.find("field_list")
            result[self.key(field)] = self.fields(nested, cells) if nested is not None else next(cells)
        return result

    def value(self, node):
        if node.tag == "value":
            return self.primitive(next(child for child in node if child.tag != "comment"))
        if node.tag == "object":
            return {self.key(pair.find("key")): self.value(pair) for pair in node.findall("pair")}
        header = node.find("keyed_header")
        keyed = header is not None
        if header is None:
            header = node.find("header")
        if header is not None:
            delimiter = header.find("delimiter")
            delimiter = "," if delimiter is None else self.text(delimiter)
            fields = header.find("field_list")
            inline = node.find("inline_values")
            if keyed:
                return {self.key(row.find("key")):
                        self.fields(fields, iter(self.cells(row.find("inline_values"), delimiter)))
                        for row in node.findall("keyed_body/keyed_row")}
            if inline is not None:
                return self.cells(inline, delimiter)
            if fields is not None:
                return [self.fields(fields, iter(self.cells(row, delimiter)))
                        for row in node.findall("array_body/tabular_row")]
            return [self.value(next(child for child in row if child.tag != "comment"))
                    for row in node.findall("array_body/row")]
        if node.tag == "object_row":
            obj = node.find("object")
            return {} if obj is None else self.value(obj)
        if node.tag == "value_row":
            array = node.find("list_array")
            return self.value(array) if array is not None else self.value(node.find("row_values/single_value/value"))
        for kind in ("object", "value"):
            child = node.find(kind)
            if child is not None:
                return self.value(child)
        return {}
