#!/usr/bin/env python3
"""Blank lines between the statements of function bodies (the rule set of AGENTS.md "Code structure").

    uv run python tools/blank_lines.py [--check] [--verbose] FILE...

Whitespace only: the tool adds blank lines inside the bodies of function definitions and lambdas and
never removes or changes anything else (clang-format collapses double blank lines and strips blank lines at
the start / end of a block). One blank line goes between two neighbouring statements when

  a. either one is a multi-line control block: for / while / do / switch / try, a bare { } block, or an
     if / else with a branch of more than one statement or more than two lines;
  b. one is an __asm statement or block and the other is not (a run of __asm statements is one unit);
  c. the first ends a run of declarations and the second is not a declaration (a short guard directly after a
     declaration is left alone: the guard checks what was just declared);
  d. the first ends a run of short guard clauses (`if (...) { return/continue/break/goto/throw ...; }`,
     one statement, at most two lines, no else) and the second is not a guard;
  e. the second is the last statement of the function body, a return, and something precedes it.

Never at the start or end of a block, never two blank lines, never between a comment and the line it
precedes (the blank goes above the comment). Nothing changes between statements on one line, next to
preprocessor lines or labels, inside initializer lists, macros, strings or __asm blocks, or in a function
that tree-sitter cannot parse cleanly. The result is idempotent and clang-format leaves it alone.

The source is parsed with tree-sitter-cpp after a same-length rewrite that hides what that grammar does not
know: calling conventions, __declspec(...), the RS7_STUB(name) function head, and __asm statements (kept as
`;` placeholders whose real extent is remembered).

Exit status: 0; with --check 1 when a file would change.
"""

from __future__ import annotations

import argparse
import sys
from bisect import bisect_right
from dataclasses import dataclass, field
from pathlib import Path

import tree_sitter_cpp
from tree_sitter import Language, Node, Parser

_PARSER = Parser(Language(tree_sitter_cpp.language()))

# Words the grammar does not know; blanked out (same length) before parsing.
_BLANK_WORDS = {
    b"__cdecl",
    b"__stdcall",
    b"__fastcall",
    b"__thiscall",
    b"__vectorcall",
    b"__forceinline",
    b"__restrict",
    b"__unaligned",
    b"WINAPI",
    b"APIENTRY",
    b"CALLBACK",
    b"NTAPI",
}
# Function-head macros: NAME(arg) -> `void arg()`, padded to the original length (.clang-format "Macros").
_HEAD_MACROS = {b"RS7_STUB", b"STUB"}
_ASM_WORDS = {b"__asm", b"_asm"}
_STRING_PREFIXES = {b"L", b"u", b"U", b"u8"}


def _is_ident_start(c: int) -> bool:
    return c == 0x5F or 0x41 <= c <= 0x5A or 0x61 <= c <= 0x7A


def _is_ident(c: int) -> bool:
    return _is_ident_start(c) or 0x30 <= c <= 0x39


def _skip_string(s: bytes, i: int, quote: int) -> int:
    """Index after the string or char literal that starts at s[i]."""
    n = len(s)
    i += 1
    while i < n and s[i] != quote:
        i += 2 if s[i] == 0x5C else 1
    return min(i + 1, n)


def _skip_raw_string(s: bytes, i: int) -> int:
    """Index after the raw string literal whose opening quote is s[i]."""
    j = s.find(b"(", i)
    if j < 0:
        return len(s)
    end = s.find(b")" + s[i + 1 : j] + b'"', j)
    return len(s) if end < 0 else end + (j - i) + 1


def _skip_directive(s: bytes, i: int) -> int:
    """Index after the preprocessor directive at s[i] (`#`), following backslash continuations."""
    n = len(s)
    while i < n:
        j = s.find(b"\n", i)
        if j < 0:
            return n
        if s[j - 1 : j] == b"\\" or s[j - 2 : j] == b"\\\r":
            i = j + 1
            continue
        return j
    return n


def _match_brace(s: bytes, i: int) -> int:
    """Index after the `}` that closes the `{` at s[i], inside an __asm block (comments: // /* */ and ;)."""
    n = len(s)
    depth = 0
    while i < n:
        c = s[i]
        if c == 0x7B:
            depth += 1
        elif c == 0x7D:
            depth -= 1
            if depth == 0:
                return i + 1
        elif c == 0x3B or s[i : i + 2] == b"//":
            j = s.find(b"\n", i)
            i = n if j < 0 else j
            continue
        elif s[i : i + 2] == b"/*":
            j = s.find(b"*/", i + 2)
            i = n if j < 0 else j + 2
            continue
        elif c == 0x22:
            i = _skip_string(s, i, c)
            continue
        i += 1
    return n


def _asm_line_end(s: bytes, i: int) -> int:
    """End of an `__asm instruction` statement: the line end, the next __asm, a `}` or a comment."""
    n = len(s)
    while i < n:
        c = s[i]
        if c in (0x0A, 0x7D) or s[i : i + 2] in (b"//", b"/*"):
            break
        if _is_ident_start(c):
            j = i
            while j < n and _is_ident(s[j]):
                j += 1
            if s[i:j] in _ASM_WORDS:
                break
            i = j
            continue
        i += 1
    while i > 0 and s[i - 1] in b" \t\r":
        i -= 1
    return i


def sanitize(src: bytes) -> tuple[bytes, dict[int, int]]:
    """(same-length text the grammar can parse, {start byte of an __asm statement: end byte})."""
    out = bytearray(src)
    asm: dict[int, int] = {}
    n = len(src)

    def blank(a: int, b: int) -> None:
        for k in range(a, b):
            if out[k] not in (0x0A, 0x0D):
                out[k] = 0x20

    i = 0
    line_start = True
    while i < n:
        c = src[i]
        if c == 0x0A:
            line_start = True
            i += 1
            continue
        if c in (0x20, 0x09, 0x0D):
            i += 1
            continue
        if c == 0x23 and line_start:
            i = _skip_directive(src, i)
            continue
        line_start = False
        if src[i : i + 2] == b"//":
            j = src.find(b"\n", i)
            i = n if j < 0 else j
        elif src[i : i + 2] == b"/*":
            j = src.find(b"*/", i + 2)
            i = n if j < 0 else j + 2
            line_start = False
        elif c == 0x22:
            raw = i > 0 and src[i - 1] == 0x52
            i = _skip_raw_string(src, i) if raw else _skip_string(src, i, c)
        elif c == 0x27:
            j = i
            while j > 0 and _is_ident(src[j - 1]):
                j -= 1
            if i > 0 and _is_ident(src[i - 1]) and src[j:i] not in _STRING_PREFIXES:
                i += 1  # digit separator
            else:
                i = _skip_string(src, i, c)
        elif _is_ident_start(c):
            j = i
            while j < n and _is_ident(src[j]):
                j += 1
            word = src[i:j]
            if word in _BLANK_WORDS:
                blank(i, j)
            elif word == b"__declspec":
                k = j
                while k < n and src[k] in b" \t":
                    k += 1
                if src[k : k + 1] == b"(":
                    depth = 0
                    while k < n:
                        depth += (src[k] == 0x28) - (src[k] == 0x29)
                        k += 1
                        if depth == 0:
                            break
                    blank(i, k)
                    j = k
            elif word in _HEAD_MACROS:
                k = j
                while k < n and src[k] in b" \t":
                    k += 1
                if src[k : k + 1] == b"(":
                    close = src.find(b")", k)
                    if close > 0 and b"\n" not in src[k:close]:
                        head = b"void " + src[k + 1 : close].strip() + b"()"
                        if len(head) <= close + 1 - i:
                            blank(i, close + 1)
                            out[i : i + len(head)] = head
                            j = close + 1
            elif word in _ASM_WORDS:
                k = j
                while k < n and src[k] in b" \t":
                    k += 1
                end = _match_brace(src, k) if src[k : k + 1] == b"{" else _asm_line_end(src, k)
                blank(i, end)
                out[i] = 0x3B
                asm[i] = end
                j = end
            i = j
        else:
            i += 1
    return bytes(out), asm


_JUMPS = {"return_statement", "continue_statement", "break_statement", "goto_statement", "throw_statement"}
_DECLS = {
    "declaration",
    "alias_declaration",
    "using_declaration",
    "type_definition",
    "static_assert_declaration",
    "namespace_alias_definition",
}
_BIG_LOOPS = {
    "for_statement",
    "for_range_loop",
    "while_statement",
    "do_statement",
    "switch_statement",
    "try_statement",
    "seh_try_statement",
}
# Nodes whose insides are never touched.
_OPAQUE = {"initializer_list", "field_initializer_list"}


@dataclass
class Item:
    kind: str  # decl guard big asm return other label barrier
    start_row: int
    end_row: int
    lead_row: int  # first row including the comments attached above
    line_start: bool  # nothing but whitespace before the item (or its first comment) on its row


@dataclass
class Result:
    text: bytes
    inserted: int = 0
    functions: int = 0
    skipped: list[int] = field(default_factory=list)  # 1-based rows of functions left alone (parse errors)


class _Pass:
    def __init__(self, src: bytes):
        self.src = src
        self.rows = src.split(b"\n")
        self.text, self.asm = sanitize(src)
        self.tree = _PARSER.parse(self.text)
        self.line_starts = [0] + [i + 1 for i, c in enumerate(self.text) if c == 0x0A]
        self.blank_row = [not r.strip() for r in self.rows]
        self.insert: set[int] = set()
        self.functions = 0
        self.skipped: list[int] = []
        self.asm_rows = {a: (self._row(a), self._row(max(a, b - 1))) for a, b in self.asm.items()}

    # Rows and columns come from byte offsets: tree-sitter 0.26's Node.start_point / end_point crash the
    # interpreter or return garbage on some nodes.
    def _row(self, byte: int) -> int:
        return bisect_right(self.line_starts, byte) - 1

    def _srow(self, node: Node) -> int:
        return self._row(node.start_byte)

    def _erow(self, node: Node) -> int:
        return self._row(max(node.end_byte - 1, node.start_byte))

    def _scol(self, node: Node) -> int:
        return node.start_byte - self.line_starts[self._srow(node)]

    # ---- classification ----------------------------------------------------------------------------

    def _content_lines(self, body: Node) -> int:
        a, b = self._srow(body), self._erow(body)
        if a == b:
            return 1
        return sum(1 for r in range(a + 1, b) if not self.blank_row[r])

    def _span_lines(self, node: Node) -> int:
        a, b = self._srow(node), self._erow(node)
        return sum(1 for r in range(a, b + 1) if not self.blank_row[r])

    @staticmethod
    def _stmts(body: Node) -> list[Node]:
        if body.type != "compound_statement":
            return [body]
        return [c for c in body.children if c.is_named and c.type != "comment"]

    def _branch_big(self, body: Node) -> bool:
        if body.type == "if_statement":
            return self._if_big(body)
        return len(self._stmts(body)) > 1 or (body.type == "compound_statement" and self._content_lines(body) > 2)

    def _if_big(self, node: Node) -> bool:
        cons = node.child_by_field_name("consequence")
        alt = node.child_by_field_name("alternative")
        if cons is not None and self._branch_big(cons):
            return True
        if alt is not None:
            inner = [c for c in alt.children if c.is_named and c.type != "comment"]
            if inner and self._branch_big(inner[0]):
                return True
        return False

    def _is_guard(self, node: Node) -> bool:
        if node.child_by_field_name("alternative") is not None:
            return False
        cons = node.child_by_field_name("consequence")
        if cons is None:
            return False
        stmts = self._stmts(cons)
        if len(stmts) != 1 or stmts[0].type not in _JUMPS:
            return False
        return cons.type != "compound_statement" or self._content_lines(cons) <= 2

    def _kind(self, node: Node) -> tuple[str, int]:
        """(kind, end row) of one statement."""
        t = node.type
        end = self._erow(node)
        if node.start_byte in self.asm:
            return "asm", self.asm_rows[node.start_byte][1]
        if t.startswith("preproc"):
            return "barrier", end
        if t in ("labeled_statement", "case_statement"):
            return "label", end
        if t in _DECLS:
            return "decl", end
        if t in _BIG_LOOPS or t == "compound_statement":
            return ("big" if self._span_lines(node) >= 3 else "other"), end
        if t == "if_statement":
            if self._is_guard(node):
                return "guard", end
            return ("big" if self._if_big(node) else "other"), end
        if t == "return_statement":
            return "return", end
        return "other", end

    # ---- blocks ------------------------------------------------------------------------------------

    def _own_line(self, row: int, col: int) -> bool:
        return not self.rows[row][:col].strip()

    def _items(self, children: list[Node]) -> list[Item]:
        items: list[Item] = []
        pending: list[Node] = []
        for c in children:
            if c.type == "comment":
                if items and self._srow(c) <= items[-1].end_row:
                    items[-1].end_row = max(items[-1].end_row, self._erow(c))
                else:
                    pending.append(c)
                continue
            if not c.is_named and c.type != ";":
                continue
            kind, end = self._kind(c)
            start = self.asm_rows[c.start_byte][0] if kind == "asm" else self._srow(c)
            lead = start
            col = self._scol(c)
            for cm in reversed(pending):
                if self._erow(cm) + 1 >= lead and self._own_line(self._srow(cm), self._scol(cm)):
                    lead = self._srow(cm)
                    col = self._scol(cm)
                else:
                    break
            pending = []
            items.append(Item(kind, start, end, lead, self._own_line(lead, col)))
        return items

    @staticmethod
    def _wants_blank(a: Item, b: Item, b_is_final_return: bool) -> bool:
        if "barrier" in (a.kind, b.kind) or "label" in (a.kind, b.kind):
            return False
        if "big" in (a.kind, b.kind):
            return True
        if (a.kind == "asm") != (b.kind == "asm"):
            return True
        if a.kind == "decl" and b.kind not in ("decl", "guard"):
            return True
        if a.kind == "guard" and b.kind != "guard":
            return True
        return b_is_final_return

    def _block(self, children: list[Node], function_level: bool) -> None:
        items = self._items(children)
        for k in range(1, len(items)):
            a, b = items[k - 1], items[k]
            final = function_level and k == len(items) - 1 and b.kind == "return"
            if not self._wants_blank(a, b, final) or not b.line_start or b.lead_row <= a.end_row:
                continue
            if any(self.blank_row[r] for r in range(a.end_row + 1, b.lead_row)):
                continue
            self.insert.add(b.lead_row)

    def _function(self, body: Node) -> None:
        """One function or lambda body: processed as a whole, or left alone when it does not parse cleanly."""
        self.functions += 1
        if body.has_error:
            self.skipped.append(self._srow(body) + 1)
            return
        self._block(self._inside(body), True)
        for c in body.children:
            self._nested(c)

    @staticmethod
    def _inside(compound: Node) -> list[Node]:
        return [c for c in compound.children if c.type not in ("{", "}")]

    def _nested(self, node: Node) -> None:
        """The blocks below a statement of a function body (lambdas start their own function)."""
        t = node.type
        if t.startswith("preproc") or t in _OPAQUE:
            return
        if t == "lambda_expression":
            body = node.child_by_field_name("body")
            if body is not None and body.type == "compound_statement":
                self._function(body)
            return
        if t == "compound_statement":
            self._block(self._inside(node), False)
        elif t == "case_statement":
            kids = node.children
            rest = kids[next((i for i, c in enumerate(kids) if c.type == ":"), -1) + 1 :]
            self._block(rest, False)
            for c in rest:
                self._nested(c)
            return
        for c in node.children:
            self._nested(c)

    def walk(self, node: Node) -> None:
        """Find the function and lambda bodies of the file."""
        if node.type in _OPAQUE:
            return
        if node.type in ("function_definition", "lambda_expression"):
            body = node.child_by_field_name("body")
            if body is not None and body.type == "compound_statement":
                self._function(body)
                return
        for c in node.children:
            self.walk(c)


def process(src: bytes) -> Result:
    """The source with the missing blank lines inserted (the same bytes when there is nothing to add)."""
    p = _Pass(src)
    p.walk(p.tree.root_node)
    if not p.insert:
        return Result(src, 0, p.functions, p.skipped)
    blank = b"\r" if src.count(b"\r\n") * 2 > src.count(b"\n") else b""
    out: list[bytes] = []
    for row, line in enumerate(src.split(b"\n")):
        if row in p.insert:
            out.append(blank)
        out.append(line)
    return Result(b"\n".join(out), len(p.insert), p.functions, p.skipped)


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("files", nargs="+", type=Path)
    ap.add_argument("--check", action="store_true", help="change nothing; exit 1 if a file would change")
    ap.add_argument("--verbose", action="store_true", help="list the functions left alone (parse errors)")
    args = ap.parse_args(argv)
    changed = functions = skipped = 0
    for path in args.files:
        src = path.read_bytes()
        res = process(src)
        functions += res.functions
        skipped += len(res.skipped)
        if args.verbose:
            for row in res.skipped:
                print(f"{path}:{row}: left alone (parse error)")
        if res.text != src:
            changed += 1
            if args.check:
                print(f"{path}: {res.inserted} blank line(s) missing")
            else:
                path.write_bytes(res.text)
    print(
        f"blank_lines: {len(args.files)} files, {functions} function bodies, {skipped} left alone, "
        f"{changed} {'would change' if args.check else 'changed'}"
    )
    return 1 if (args.check and changed) else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
