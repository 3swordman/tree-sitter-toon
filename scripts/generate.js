// Generate the grammar without replacing independently maintained C headers.
// In particular, array.h includes the upstream strict-aliasing and C++ fixes.
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const { execFileSync } = require("node:child_process");

const root = path.join(__dirname, "..");
const output = fs.mkdtempSync(path.join(os.tmpdir(), "tree-sitter-toon-"));
try {
  execFileSync(process.execPath, [
    require.resolve("tree-sitter-cli/cli.js"),
    "generate", "--abi", "15", "--output", output,
  ], { cwd: root, stdio: "inherit" });
  for (const file of ["parser.c", "grammar.json", "node-types.json"]) {
    fs.copyFileSync(path.join(output, file), path.join(root, "src", file));
  }
} finally {
  fs.rmSync(output, { recursive: true, force: true });
}
