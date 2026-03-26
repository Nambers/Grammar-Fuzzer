import json
import sys
from pathlib import Path

MARKER = "===AST==="


def _parse_first_json_object(text: str):
    decoder = json.JSONDecoder()
    obj, _ = decoder.raw_decode(text.lstrip())
    return obj


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

    # Newer logs already contain the full AST JSON object and optionally have
    # ---DECL_END--- / ---CURRENT_NODE--- markers with no history payload.
    # Older logs contain declarations JSON + trailing expression history.
    if "---DECL_END---" not in ast_text:
        ast_obj = _parse_first_json_object(ast_text)
    else:
        decl_text, rest = ast_text.split("---DECL_END---", 1)
        ast_obj = json.loads(decl_text)

        history_text = rest.replace("\n---CURRENT_NODE---\n", "").strip()
        if history_text.endswith(","):
            history_text = history_text[:-1].rstrip()

        # Only synthesize expressions when old-style history is actually present.
        if history_text:
            history = json.loads(f"[{history_text}]")
            ast_obj["scopes"][0]["expressions"] = list(range(len(history)))
            ast_obj["expressions"] = history

    out_path.write_text(
        json.dumps(ast_obj, indent=2, ensure_ascii=False), encoding="utf-8"
    )


if __name__ == "__main__":
    main()
