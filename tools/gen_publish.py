"""Generate verified publish-preparation function copies and entry sites from the unpacked exe.

    uv run python tools/gen_publish.py
No archive or game-folder access. The reviewed edits are local to this checkout.
"""

from pathlib import Path
import struct
import gen_fncopy as fc

REPO = Path(__file__).resolve().parents[1]
OUT = REPO / "src/core/publish_specs.generated.h"
FNS = [
    fc.Fn(
        "PUBCL",
        0x8750C0,
        0x875424,
        arrays=False,
        edits={
            0x8751FC: ("call", "Rs7_Call_PubBaseG", 7),
            0x875205: ("call", "Rs7_Call_PubBaseB", 7),
            0x87522C: ("call", "Rs7_Call_PubClassify", 7),
            0x875233: (
                "bytes",
                "B9 46 00 00 00 83 C3 08 EB 03",
                (b"\xe9" + struct.pack("<i", 0x8753D2 - 0x875238) + b"\x90" * 5).hex(),
            ),
        },
    ),
    fc.Fn(
        "PUBTO",
        0x4E58E0,
        0x4E5936,
        arrays=False,
        edits={
            0x4E58E7: ("bytes", "05 4C 13 00 00", "05 C8 13 00 00"),
            0x4E58EE: ("bytes", "0F BF 4E 44", "0F BF 4E 58"),
        },
    ),
    fc.Fn(
        "PUBWU",
        0x58E790,
        0x58E820,
        arrays=False,
        edits={
            0x58E7A2: ("call", "Rs7_Call_PubWireCount", 7),
        },
    ),
]


def results():
    text, base = fc.load_text()
    return [(f, *fc.build(f, text, base)) for f in FNS]


def render():
    return (
        fc.render(results())
        .replace("tools/gen_fncopy.py", "tools/gen_publish.py")
        .replace("kRs7FnSpecs", "kRs7PubSpecs")
        .replace("kRs7FnSpecCount", "kRs7PubSpecCount")
    )


if __name__ == "__main__":
    OUT.write_text(render(), encoding="utf-8", newline="\n")
    print(f"{OUT}: {len(FNS)} functions")
