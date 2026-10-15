"""tools/blank_lines.py: the blank-line rules of AGENTS.md "Code structure", on small inputs and on the tree."""

from __future__ import annotations

import subprocess
from pathlib import Path


import blank_lines as bl


REPO = Path(__file__).resolve().parents[2]


def fmt(src: str) -> str:
    return bl.process(src.encode()).text.decode()


def check(before: str, after: str | None = None) -> None:
    """`before` becomes `after` (default: unchanged), and the result is a fixed point."""
    out = fmt(before)
    assert out == (before if after is None else after)
    assert fmt(out) == out


def test_declarations_then_statements_get_one_blank_line():
    check(
        "void f() {\n    int a = 1;\n    int b = 2;\n    use(a, b);\n    use(b);\n}\n",
        "void f() {\n    int a = 1;\n    int b = 2;\n\n    use(a, b);\n    use(b);\n}\n",
    )


def test_loops_switches_and_multi_statement_ifs_are_surrounded():
    check(
        "void f() {\n    a();\n    for (int i = 0; i < 3; ++i) {\n        b(i);\n    }\n    c();\n"
        "    if (x) {\n        d();\n        e();\n    }\n    g();\n}\n",
        "void f() {\n    a();\n\n    for (int i = 0; i < 3; ++i) {\n        b(i);\n    }\n\n    c();\n\n"
        "    if (x) {\n        d();\n        e();\n    }\n\n    g();\n}\n",
    )


def test_small_ifs_are_not_control_blocks():
    check("void f() {\n    a();\n    if (x) {\n        y = 2;\n    }\n    b();\n}\n")
    check("void f() {\n    a();\n    if (x) {\n        y = 1;\n    } else {\n        y = 2;\n    }\n    b();\n}\n")


def test_an_else_branch_can_make_an_if_big():
    check(
        "void f() {\n    a();\n    if (x) {\n        y = 1;\n    } else {\n        p();\n        q();\n    }\n    b();\n}\n",
        "void f() {\n    a();\n\n    if (x) {\n        y = 1;\n    } else {\n        p();\n        q();\n    }\n\n    b();\n}\n",
    )


def test_a_long_single_statement_body_is_big():
    check(
        "void f() {\n    a();\n    if (x) {\n        call(1,\n             2,\n             3);\n    }\n    b();\n}\n",
        "void f() {\n    a();\n\n    if (x) {\n        call(1,\n             2,\n             3);\n    }\n\n    b();\n}\n",
    )


def test_guard_clauses_hug_and_a_run_of_them_ends_with_a_blank_line():
    check(
        "int f(int* p) {\n    if (!p) {\n        return 0;\n    }\n    if (*p < 0) {\n        return 1;\n    }\n"
        "    work(p);\n    work(p);\n}\n",
        "int f(int* p) {\n    if (!p) {\n        return 0;\n    }\n    if (*p < 0) {\n        return 1;\n    }\n\n"
        "    work(p);\n    work(p);\n}\n",
    )


def test_a_guard_right_after_a_declaration_hugs_it():
    check("void f() {\n    int* p = get();\n    if (!p) {\n        return;\n    }\n}\n")


def test_guard_kinds():
    for jump in ("continue;", "break;", "return;", "goto out;", "throw 1;"):
        check(
            f"void f() {{\n    a();\n    if (x) {{\n        {jump}\n    }}\n    b();\n    c();\n}}\n",
            f"void f() {{\n    a();\n    if (x) {{\n        {jump}\n    }}\n\n    b();\n    c();\n}}\n",
        )


def test_a_guard_with_an_else_or_two_statements_is_not_a_guard():
    check(
        "void f() {\n    a();\n    if (x) {\n        log();\n        return;\n    }\n    b();\n}\n",
        "void f() {\n    a();\n\n    if (x) {\n        log();\n        return;\n    }\n\n    b();\n}\n",
    )


def test_final_return_after_other_statements():
    check("int f() {\n    a();\n    return 1;\n}\n", "int f() {\n    a();\n\n    return 1;\n}\n")
    check("int f() {\n    return 1;\n}\n")
    # only the last statement of the function body, not a return inside a nested block
    check(
        "int f() {\n    if (x) {\n        a();\n        return 2;\n    }\n}\n",
        "int f() {\n    if (x) {\n        a();\n        return 2;\n    }\n}\n",
    )


def test_asm_blocks_and_runs_of_asm_statements():
    check(
        "void f() {\n    a();\n    __asm {\n        nop\n    }\n    b();\n}\n",
        "void f() {\n    a();\n\n    __asm {\n        nop\n    }\n\n    b();\n}\n",
    )
    check(
        "void f() {\n    a();\n    __asm mov eax, 1\n    __asm ret\n    b();\n}\n",
        "void f() {\n    a();\n\n    __asm mov eax, 1\n    __asm ret\n\n    b();\n}\n",
    )
    check("void f() {\n    __asm {\n        nop\n    }\n    __asm {\n        nop\n    }\n}\n")


def test_asm_contents_are_never_touched():
    src = "RS7_STUB(Stub) {\n    __asm {\n        push eax\n\n        pop eax\n    lbl:\n        ret\n    }\n}\n"
    check(src)


def test_stub_heads_and_calling_conventions_parse():
    check(
        'extern "C" __declspec(dllexport) int __cdecl F(int x) {\n    int a = x;\n    return a;\n}\n',
        'extern "C" __declspec(dllexport) int __cdecl F(int x) {\n    int a = x;\n\n    return a;\n}\n',
    )
    check(
        "DWORD WINAPI Worker(LPVOID) {\n    const int t = 1;\n    run(t);\n}\n",
        "DWORD WINAPI Worker(LPVOID) {\n    const int t = 1;\n\n    run(t);\n}\n",
    )
    check("RS7_STUB(Stub) {\n    __asm { ret }\n}\n")


def test_comments_stay_attached_to_the_following_statement():
    check(
        "void f() {\n    int a = 1;\n    // why\n    // and more\n    use(a);\n}\n",
        "void f() {\n    int a = 1;\n\n    // why\n    // and more\n    use(a);\n}\n",
    )
    # a trailing comment belongs to its own line; a detached comment is not attached
    check(
        "void f() {\n    int a = 1;  // note\n    use(a);\n}\n",
        "void f() {\n    int a = 1;  // note\n\n    use(a);\n}\n",
    )
    check("void f() {\n    int a = 1;\n    // detached\n\n    use(a);\n}\n")


def test_existing_blank_lines_are_kept_and_never_doubled():
    check("void f() {\n    int a = 1;\n\n    use(a);\n}\n")
    check("void f() {\n    int a = 1;\n    // c\n\n    use(a);\n}\n")


def test_never_at_the_start_or_end_of_a_block():
    check("void f() {\n    for (;;) {\n        for (;;) {\n            a();\n        }\n    }\n}\n")
    check("void f() {\n    if (x) {\n        int a = 1;\n    }\n}\n")


def test_statements_on_one_line_are_left_alone():
    check("void f() {\n    int a = 1; use(a);\n    for (;;) { a(); b(); } c();\n}\n")


def test_switch_cases_are_blocks_and_labels_are_not_surrounded():
    check(
        "void f(int k) {\n    switch (k) {\n        case 1:\n            a();\n            for (;;) {\n"
        "                b();\n            }\n            break;\n        default:\n            break;\n    }\n}\n",
        "void f(int k) {\n    switch (k) {\n        case 1:\n            a();\n\n            for (;;) {\n"
        "                b();\n            }\n\n            break;\n        default:\n            break;\n    }\n}\n",
    )


def test_preprocessor_lines_are_barriers():
    check("void f() {\n    int a = 1;\n#if X\n    use(a);\n#endif\n    use(a);\n}\n")


def test_lambda_bodies_are_function_bodies():
    check(
        "void f() {\n    auto g = [&](int x) {\n        int y = x;\n        return y;\n    };\n    g(1);\n}\n",
        "void f() {\n    auto g = [&](int x) {\n        int y = x;\n\n        return y;\n    };\n\n    g(1);\n}\n",
    )


def test_initializer_lists_and_their_lambdas_are_not_touched():
    check("const Fn kTable[] = {[](int x) {\n    int y = x;\n    return y;\n}};\n")
    check("void f() {\n    const Fn k[] = {[] {\n        int a = 1;\n        use(a);\n    }};\n}\n")


def test_strings_and_comments_hide_keywords():
    check(
        'void f() {\n    const char* s = "__asm { }";\n    use(s);\n}\n',
        'void f() {\n    const char* s = "__asm { }";\n\n    use(s);\n}\n',
    )
    check("void f() {\n    // __asm {\n    use();\n}\n")


def test_a_function_that_does_not_parse_is_left_alone_and_reported():
    src = "void f() {\n    int a = ;\n    use(a);\n    use(a);\n}\n"
    res = bl.process(src.encode())
    assert res.text.decode() == src
    assert res.skipped == [1]
    assert "int a = 1;" in fmt("void g() {\n    int a = 1;\n    use(a);\n}\n")


def test_crlf_files_get_crlf_blank_lines():
    out = bl.process(b"void f() {\r\n    int a = 1;\r\n    use(a);\r\n}\r\n").text
    assert out == b"void f() {\r\n    int a = 1;\r\n\r\n    use(a);\r\n}\r\n"


def test_sanitize_keeps_length_and_newlines():
    src = b'extern "C" __declspec(dllexport) int __cdecl F() {\n  __asm {\n    nop\n  }\n  RS7_STUB(x) \n}\n'
    text, asm = bl.sanitize(src)
    assert len(text) == len(src)
    assert [i for i, c in enumerate(text) if c == 10] == [i for i, c in enumerate(src) if c == 10]
    assert list(asm.values()) == [src.index(b"}\n  RS7") + 1]


def tracked_cpp() -> list[Path]:
    out = subprocess.run(["git", "ls-files"], cwd=REPO, capture_output=True, text=True, check=True).stdout.split()
    keep = [p for p in out if p.startswith(("src/", "tests/")) and p.endswith((".cpp", ".h"))]
    return [REPO / p for p in keep if ".generated.h" not in p]


def test_the_tree_has_its_blank_lines():
    """Run `scripts/format.ps1` when this fails (clang-format first: line counts decide what is "big")."""
    files = tracked_cpp()
    assert len(files) > 100
    bad = [str(p.relative_to(REPO)) for p in files if bl.process(p.read_bytes()).text != p.read_bytes()]
    assert not bad, bad
