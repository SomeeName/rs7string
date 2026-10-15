"""Add the 7-string key to the arrangement manifests of a plain (unencrypted) PSARC.

    uv run python rs7string/tools/manifest_key.py IN.psarc OUT.psarc [--t6 N] [--arr NAME]

The pre-song tuner opens before song setup and reads the tuning from the manifest `Tuning` object
(docs/ARCHITECTURE.md), not from the SNG. rs7string reads the 7th string's offset
from the key `Rs7String6` there. The key must NOT be `string6`: the song-list reader 0x56d0b0
writes every `string%d` key into a 6-int stack array and `string6` overwrites the /GS cookie, which
crashes the stock game. Another name only raises the child count; its `string6` lookup misses.

Adds `"Rs7String6": t6` after `"string5"` in every `Tuning` object of the arrangement .json
manifests and the song .hsan (both, so either source carries it). Bass and vocals manifests are
skipped (no `Tuning` with string5 / ArrangementName Bass). Text-level edit: the rest of each file
stays byte for byte.
"""

import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import psarc_plain  # noqa: E402

KEY = "Rs7String6"
_STRING5 = re.compile(r'("string5"\s*:\s*-?\d+(?:\.\d+)?)(\s*)(\})')


def add_key(text: str, t6: int) -> tuple[str, int]:
    """Returns (new text, number of Tuning objects changed). Idempotent: an existing key is kept."""
    if f'"{KEY}"' in text:
        return text, 0
    out, n, pos = [], 0, 0
    for m in _STRING5.finditer(text):
        # Only inside a "Tuning" object of a non-bass arrangement block.
        head = text.rfind('"Tuning"', 0, m.start())
        block = text.rfind('"ArrangementName"', 0, m.start())
        if head < 0 or text.rfind("{", head, m.start()) < 0:
            continue
        if block >= 0 and re.match(r'"ArrangementName"\s*:\s*"Bass"', text[block : block + 40]):
            continue
        line_start = text.rfind("\n", 0, m.start()) + 1
        indent = re.match(r"[ \t]*", text[line_start:]).group(0)
        nl = "\r\n" if "\r\n" in m.group(2) or text[m.end(1) : m.end(1) + 2] == "\r\n" else "\n"
        out.append(text[pos : m.end(1)])
        out.append(f',{nl}{indent}"{KEY}": {t6}')
        pos = m.end(1)
        n += 1
    out.append(text[pos:])
    return "".join(out), n


def main(argv):
    if len(argv) < 2:
        sys.exit(__doc__)
    src, dst = argv[0], argv[1]
    t6 = int(argv[argv.index("--t6") + 1]) if "--t6" in argv else 0
    arr = argv[argv.index("--arr") + 1].lower() if "--arr" in argv else None
    files, meta = psarc_plain.read(src)
    total = 0
    for name in meta["names"]:
        low = name.lower()
        if not low.startswith("manifests/") or not (low.endswith(".json") or low.endswith(".hsan")):
            continue
        if low.endswith(".json") and arr and not low.endswith(f"_{arr}.json"):
            continue
        text = files[name].decode("utf-8")
        new, n = add_key(text, t6)
        if n:
            files[name] = new.encode("utf-8")
            print(f"{name}: {n} Tuning object(s) +{KEY}={t6}")
            total += n
    if not total:
        sys.exit("no Tuning object changed (key present already, or no guitar manifest)")
    psarc_plain.write(dst, files, meta["names"], meta["ver"], meta["flags"])
    print("wrote", dst)


if __name__ == "__main__":
    main(sys.argv[1:])
