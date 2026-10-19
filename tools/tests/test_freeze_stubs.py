"""freeze_stubs.check_target: a frozen fixup must resolve to a known export (no DLL needed)."""

from __future__ import annotations

import pytest

import freeze_stubs


class FakeImage:
    arrays = ["g_table"]
    exports = {"Rs7_Helper": 0x1000, "g_var": 0x9000, "g_table": 0x9100}

    def is_code(self, va: int) -> bool:
        return va < 0x8000


@pytest.mark.parametrize(
    "sym,add,relative",
    [("Rs7_Helper", 0, True), ("g_var", 0, False), ("g_var", 8, False), ("g_table", -24, False)],
)
def test_known_exports_pass(sym, add, relative):
    freeze_stubs.check_target(FakeImage(), "Rs7_Stub_X", 0x5000, sym, add, relative)


@pytest.mark.parametrize(
    "sym,add,relative,why",
    [
        ("Rs7_Helper", 0x90, True, "not an exported function"),  # a call into a non-exported neighbour
        ("g_var", -4, False, "not a hinted array"),
        ("Rs7_Helper", 4, False, "inside a function"),
    ],
)
def test_unknown_targets_fail(sym, add, relative, why):
    with pytest.raises(AssertionError, match=why):
        freeze_stubs.check_target(FakeImage(), "Rs7_Stub_X", 0x5000, sym, add, relative)


def test_map_symbols_name_a_non_exported_function(tmp_path):
    """--local=NAME: the map's `_NAME` entry becomes a symbol, so a call to it freezes with addend 0."""
    img = FakeImage()
    img.exports = dict(FakeImage.exports)
    mapfile = tmp_path / "x.map"
    mapfile.write_text(
        " 0001:00000100       _Rs7_Local                 00001100 f   a.obj\n"
        " 0001:00000200       __catch$_Rs7_Local$0       00001200 f   a.obj\n"
    )
    freeze_stubs.add_map_symbols(img, mapfile, ["Rs7_Local"])
    assert img.exports["Rs7_Local"] == 0x1100
    freeze_stubs.check_target(img, "Rs7_Stub_X", 0x5000, *img_symbol(img, 0x1100), True)
    with pytest.raises(AssertionError, match="not in"):
        freeze_stubs.add_map_symbols(img, mapfile, ["Rs7_Missing"])


def img_symbol(img, va):
    return next((n, va - a) for a, n in sorted(((a, n) for n, a in img.exports.items()), reverse=True) if a <= va)
