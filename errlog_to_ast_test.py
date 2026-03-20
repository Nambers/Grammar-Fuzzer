import json
import sys
from pathlib import Path

MARKER = "===AST==="


def main() -> None:
    if len(sys.argv) != 2:
        print(f"Usage: {Path(sys.argv[0]).name} <path>")
        sys.exit(1)

    base = Path(sys.argv[1])
    errlog_path = base / "errlog.txt"
    out_path = base / "ast_test.json"

    if not errlog_path.exists():
        raise FileNotFoundError(f"Missing file: {errlog_path}")

    text = errlog_path.read_text(encoding="utf-8")

    if MARKER not in text:
        raise ValueError(f"Marker '{MARKER}' not found in {errlog_path}")

    ast_text = text.split(MARKER, 1)[1].lstrip()
    decls = ast_text.split("---DECL_END---")[0]
    history = (
        "["
        + ast_text.split("---DECL_END---")[1].replace("\n---CURRENT_NODE---\n", "").removesuffix(",")
        + "]"
    )

    decls = json.loads(decls)
    history = json.loads(history)

    decls["scopes"][0]["expressions"] = list(range(len(history)))
    decls["expressions"] = history

    out_path.write_text(
        json.dumps(decls, indent=2, ensure_ascii=False), encoding="utf-8"
    )


if __name__ == "__main__":
    main()
