#!/usr/bin/env python3
"""Emit publication grid sites and registry fragments; never mutates shared registry/sites.
Run from repo: uv run python tools/gen_publish_grid.py > docs/publish-grid-manifest.json
CONFIRMED original bytes from September2022 unpacked PE. Hook patterns intentionally avoid
existing native-layout changed operands. Parent must replace FGRB registry entry with PUBGB.
"""

import json
import gen_fncopy

SPECS = [
    ("PUBGS", 0x5903BD, 8, 0x59044E),
    ("PUBGE1", 0x7E3488, 9, 0x7E36E8),
    ("PUBGE2", 0x7E367D, 9, 0x7E36E8),
    ("PUBGC", 0x58F8AF, 6, None),
    ("PUBGO", 0x58FCF6, 6, None),
    ("PUBGB", 0x58FA25, 5, None),
    ("PUBGA", 0x594530, 6, None),
    ("PUBGF", 0x58CF90, 5, None),
    ("PUBEDGE", 0x7E72D0, 13, None),
]


def manifest():
    text, base = gen_fncopy.load_text()
    sites = []
    registry = []
    for name, va, length, target in SPECS:
        raw = text[va - base : va - base + length]
        # Include24+ context bytes; mask any instruction-relative operands and absolute pointers.
        pattern = bytearray(text[va - base : va - base + 32])
        tokens = [f"{v:02X}" for v in pattern]
        import capstone

        md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
        md.detail = True
        for i in md.disasm(bytes(pattern), va):
            if i.group(capstone.CS_GRP_JUMP) or i.group(capstone.CS_GRP_CALL):
                for k in range(i.address - va + i.imm_offset, i.address - va + i.imm_offset + i.imm_size):
                    tokens[k] = "??"
            for op in i.operands:
                if op.type == capstone.x86.X86_OP_MEM and op.mem.base == 0 and i.disp_size == 4:
                    for k in range(i.address - va + i.disp_offset, i.address - va + i.disp_offset + 4):
                        tokens[k] = "??"
        site = {
            "id": name,
            "kind": "jmp",
            "length": length,
            "pattern": " ".join(tokens),
            "offset": "0x0",
            "expect": raw.hex(" ").upper(),
            "publish": "native",
            "note": f"0x{va:X} publication grid exact port",
        }
        if target:
            # Last overwritten instruction is the stock JAE rel32.
            site["target"] = {"rel_at": length - 4, "rel_size": 4, "insn_end": length}
        sites.append(site)
        registry.append(
            f'{{ "{name}", Rs7_Stub_{name}, &g_rs7_resume_{name}, '
            + (f"&g_rs7_target_{name}" if target else "nullptr")
            + ", kRs7Publish },"
        )
    return {
        "sites": sites,
        "registry": registry,
        "replace_registry": "FGRB is disabled while publication ports on; PUBGB shares58FA25",
    }


if __name__ == "__main__":
    print(json.dumps(manifest(), indent=2))
