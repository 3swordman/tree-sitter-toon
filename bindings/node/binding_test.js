const assert = require("node:assert");
const { test } = require("node:test");
const fs = require("node:fs");
const path = require("node:path");

const Parser = require("tree-sitter");
const Toon = require(".");

test("can load grammar", () => {
  const parser = new Parser();
  assert.doesNotThrow(() => parser.setLanguage(Toon));
});

test("parses all TOON 4.4 fixture inputs with the native binding", () => {
  const parser = new Parser();
  parser.setLanguage(Toon);
  for (const kind of ["decode", "encode"]) {
    const fixtures = require(`../../test/fixtures/v4-${kind}.json`);
    for (const fixture of fixtures.tests) {
      const source = kind === "decode" ? fixture.input : fixture.expected;
      const tree = parser.parse(source);
      assert.equal(tree.rootNode.hasError, false, `${kind}: ${fixture.name}\n${tree.rootNode}`);
    }
  }
});

test("editor queries compile against the generated grammar", () => {
  for (const name of ["highlights", "indents", "brackets"]) {
    const source = fs.readFileSync(path.join(__dirname, "../../queries", `${name}.scm`), "utf8");
    assert.doesNotThrow(() => new Parser.Query(Toon, source), name);
  }
});
