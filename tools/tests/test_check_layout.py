"""tools/check_layout.py: baseline comparison and the include direction rules."""

import check_layout


def test_baseline_allows_known_and_shrunk_violations():
    base = {"file_lines": {"a.cpp": 900}, "function_lines": {}, "asm": {"s.cpp": 4}}
    now = {"file_lines": {"a.cpp": 850}, "function_lines": {}, "asm": {"s.cpp": 4}}

    assert check_layout.compare(now, base) == []


def test_baseline_rejects_new_grown_and_stale_entries():
    base = {"file_lines": {"a.cpp": 900, "gone.cpp": 1000}, "function_lines": {}, "asm": {}}
    now = {"file_lines": {"a.cpp": 901, "new.cpp": 801}, "function_lines": {}, "asm": {"x.cpp": 1}}
    errors = "\n".join(check_layout.compare(now, base))

    assert "a.cpp: over 800 lines (901) - grew from 900" in errors
    assert "new.cpp: over 800 lines (801) - new violation" in errors
    assert "gone.cpp: fixed" in errors
    assert "x.cpp: inline assembly (__asm) (1) - new violation" in errors


def test_layers():
    assert check_layout.layer("platform/patch.h") == ("platform", "")
    assert check_layout.layer("model/strings.h") == ("model", "")
    assert check_layout.layer("features/tuner/tuner.h") == ("feature", "tuner")
    assert check_layout.layer("hooks/registry.h") == ("unknown", "")  # the old layout is gone
    assert check_layout.layer("core/neck.h") == ("unknown", "")
    assert check_layout.layer("d3d/swap.h") == ("app", "")
    assert check_layout.layer("overlay/ui.h") == ("app", "")
    assert check_layout.layer("log.h") == ("app", "")


def _tree(tmp_path, files):
    for rel, text in files.items():
        p = tmp_path / "src" / rel
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(text)

    return sorted((tmp_path / "src").rglob("*.*"))


def test_include_direction(tmp_path, monkeypatch):
    monkeypatch.setattr(check_layout, "REPO", tmp_path)
    files = _tree(
        tmp_path,
        {
            "platform/patch.cpp": '#include "platform/patch.h"\n#include "model/strings.h"\n',
            "model/strings.cpp": '#include "platform/log.h"\n#include "features/tuner/tuner.h"\n',
            "features/tuner/hooks.cpp": '#include "features/tuner/logic.h"\n#include "features/pegs/pegs.h"\n'
            '#include "features/pegs/logic.h"\n',
            "overlay/ui.cpp": '#include "features/tuner/tuner.h"\n#include "features/tuner/logic.h"\n',
        },
    )
    errors = "\n".join(check_layout.include_errors(files))

    assert "platform/patch.cpp: platform includes model/strings.h" in errors
    assert "model/strings.cpp: model includes features/tuner/tuner.h" in errors
    assert "features/tuner/hooks.cpp: includes features/pegs/logic.h, an internal of feature pegs" in errors
    assert "overlay/ui.cpp: includes features/tuner/logic.h" in errors
    assert errors.count("\n") == 3  # four errors: own-feature and public-header includes are fine


def _errors(tmp_path, monkeypatch, files):
    monkeypatch.setattr(check_layout, "REPO", tmp_path)

    return check_layout.include_errors(_tree(tmp_path, files))


def test_feature_may_not_include_the_app_layer(tmp_path, monkeypatch):
    errors = _errors(
        tmp_path,
        monkeypatch,
        {
            "features/tuner/hooks.cpp": '#include "d3d/swap.h"\n#include "overlay/ui.h"\n#include "log.h"\n',
            "features/tuner/ok.cpp": '#include "platform/log.h"\n#include "model/eight.h"\n',
        },
    )

    assert [e.split(":")[0] for e in errors] == ["features/tuner/hooks.cpp"] * 3
    assert "feature includes d3d/swap.h (app layer)" in errors[0]
    assert "feature includes overlay/ui.h" in errors[1]
    assert "feature includes log.h" in errors[2]


def test_app_may_include_everything_through_public_headers(tmp_path, monkeypatch):
    errors = _errors(
        tmp_path,
        monkeypatch,
        {
            "dllmain.cpp": '#include "d3d/swap.h"\n#include "features/tuner/tuner.h"\n#include "model/eight.h"\n',
            "d3d/swap.cpp": '#include "platform/log.h"\n#include "features/tuner/logic.h"\n',
        },
    )

    assert len(errors) == 1
    assert "d3d/swap.cpp: includes features/tuner/logic.h, an internal of feature tuner" in errors[0]


def test_platform_and_model_may_not_include_the_app_layer(tmp_path, monkeypatch):
    errors = "\n".join(
        _errors(
            tmp_path,
            monkeypatch,
            {
                "platform/a.cpp": '#include "log.h"\n#include "d3d/swap.h"\n',
                "model/b.cpp": '#include "overlay/ui.h"\n#include "platform/log.h"\n',
            },
        )
    )

    for needle in (
        "platform includes log.h",
        "platform includes d3d/swap.h",
        "model includes overlay/ui.h",
    ):
        assert needle in errors

    assert errors.count("\n") == 2


def test_third_party_headers_are_ignored_and_unknown_folders_reported(tmp_path, monkeypatch):
    errors = _errors(
        tmp_path,
        monkeypatch,
        {"features/tuner/a.cpp": '#include "backends/imgui.h"\n', "stray/x.cpp": '#include "platform/log.h"\n'},
    )

    assert errors == ["stray/x.cpp: in no layer (src/platform, model, features/<name>, d3d, overlay or src/*)"]


def test_function_length_ignores_inline_assembly(tmp_path):
    # Inline assembly (here `[ebp + 8]`, `ret 4`) used to make tree-sitter run the first function on into the next.
    src = tmp_path / "a.cpp"
    src.write_text(
        "int Call() {\n"
        "    __asm {\n"
        "        push ebp\n"
        "        mov ecx, [ebp + 8]\n"
        "        add esp, 0x18\n"
        "        ret 4\n"
        "    }\n"
        "    return 0;\n"
        "}\n"
        "\n"
        "int Next() {\n"
        "    return 1;\n"
        "}\n"
    )

    assert check_layout.cpp_functions(src) == {"Call": 9, "Next": 3}


def test_mask_asm_blocks_keeps_line_numbers():
    source = b"void f() {\n    __asm {\n        ret\n    }\n}\n"
    masked = check_layout.mask_asm_blocks(source)

    assert len(masked) == len(source) and masked.count(b"\n") == source.count(b"\n")
    assert b"__asm" not in masked and b"ret" not in masked
