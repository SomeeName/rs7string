"""List byte-patch sites whose written bytes bound a string loop at 7 or 8 (offline, no game).

Usage (from the checkout): uv run python tools/audit_eight_sites.py [GITREF ...]
Default refs: HEAD. Reads sites.json at each ref with `git show REF:sites.json`.

A "bound" is an instruction in the written bytes (capstone) - cmp/mov/lea/sub with an
immediate or displacement of 7 (seven-bound) or 8 (eight-wide) - that the stock `expect` bytes
do not contain, or a single changed byte 06 -> 07/08 (data/count bytes such as PSQ1).
Other sites (jmp/call stubs) keep their bounds in C++/asm and are covered by tools/audit_eight.py (eight-audit.tsv).
Output: TSV on stdout: ref, id, bound, instruction, note.
"""

import json
import re
import subprocess
import sys

import capstone


def hexbytes(s):
    return bytes.fromhex(s.replace(" ", "")) if s else b""


MD = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
RX = re.compile(r"^(cmp|mov|lea|sub) .*(?:, |\+ )([78])\]?$")


def insns(code):
    return [f"{i.mnemonic} {i.op_str}" for i in MD.disasm(code, 0)]


def bounds(site):
    exp, wr = hexbytes(site.get("expect", "")), hexbytes(site.get("write", ""))
    if not wr:
        return []
    if len(wr) == 1 and len(exp) == 1:
        return [wr[0]] if exp[0] == 6 and wr[0] in (7, 8) else []
    stock = set(insns(exp))
    out = []
    for text in insns(wr):
        m = RX.match(text)
        if m and text not in stock:
            out.append((int(m.group(2)), text))
    return out


def main(refs):
    print("ref	id	bound	insn	note")
    for ref in refs or ["HEAD"]:
        raw = subprocess.run(["git", "show", f"{ref}:sites.json"], capture_output=True, check=True).stdout
        for s in json.loads(raw)["sites"]:
            for b in bounds(s):
                n, text = b if isinstance(b, tuple) else (b, f"byte {b:02X}")
                print(f"{ref}	{s['id']}	{n}	{text}	{s['note']}")


if __name__ == "__main__":
    main(sys.argv[1:])
