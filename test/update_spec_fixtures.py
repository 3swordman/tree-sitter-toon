"""Import syntax fixtures from a local checkout of the official TOON spec.

Usage: python3 test/update_spec_fixtures.py /path/to/toon-format/spec
Review generated tree snapshots before committing them. Exact fixture bytes are
also tested by check_v4.py, since corpus files add a final newline to inputs.
"""
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    spec = Path(sys.argv[1]).resolve()
    version = re.search(r"\*\*Version:\*\* ([\d.]+)", (spec / "SPEC.md").read_text())[1]
    revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=spec, text=True).strip()
    for kind in ("decode", "encode"):
        cases = []
        for path in sorted((spec / "tests/fixtures" / kind).glob("*.json")):
            for case in json.loads(path.read_text())["tests"]:
                if not case.get("shouldError") and "expected" in case:
                    cases.append({"category": path.stem, **case})
        target = ROOT / f"test/fixtures/v4-{kind}.json"
        target.write_text(json.dumps({
            "version": version,
            "source": f"https://github.com/toon-format/spec/tree/{revision}/tests/fixtures/{kind}",
            "revision": revision,
            "tests": cases,
        }, ensure_ascii=False, indent=2) + "\n")

        snapshots = []
        with tempfile.TemporaryDirectory() as directory:
            input_path = Path(directory) / "input.toon"
            for case in cases:
                source = case["input"] if kind == "decode" else case["expected"]
                # Parse the same newline-terminated source the corpus runner sees.
                input_path.write_bytes((source + "\n").encode("utf-8"))
                result = subprocess.run(["tree-sitter", "parse", "--no-ranges", str(input_path)],
                                        cwd=ROOT, text=True, capture_output=True, timeout=10)
                if result.returncode:
                    raise RuntimeError(f"{case['name']}:\n{result.stdout}{result.stderr}")
                snapshots.append("==================\n"
                                 f"spec v{version} {kind}: {case['category']} - {case['name']}\n"
                                 "==================\n"
                                 f"{source}\n\n---\n\n{result.stdout.strip()}\n\n")
        (ROOT / f"test/corpus/spec/{kind}.txt").write_bytes(
            ("".join(snapshots).rstrip("\n") + "\n").encode("utf-8"))
        print(f"Imported {len(cases)} {kind} cases for TOON {version} at {revision}")
    (ROOT / "test/fixtures/LICENSE").write_bytes((spec / "LICENSE").read_bytes())


if __name__ == "__main__":
    main()
