import pytest

import rsoverlay


def test_verbs_and_arguments_become_pipe_lines():
    assert rsoverlay.lines_for(["show"]) == ["o show"]
    assert rsoverlay.lines_for(["TOGGLE"]) == ["o toggle"]
    assert rsoverlay.lines_for(["tab", "neck"]) == ["o tab neck"]
    assert rsoverlay.lines_for(["set", "neck.peg7_offset", "0,", "0.5,", "1"]) == ["o set neck.peg7_offset 0, 0.5, 1"]
    assert rsoverlay.lines_for(["raw", "o show", "o tab log"]) == ["o show", "o tab log"]


@pytest.mark.parametrize(
    "argv", [[], ["explode"], ["show", "now"], ["tab"], ["set", "a"], ["raw", "k 1C 1"], ["raw", "o show\nk 1C 1"]]
)
def test_anything_else_is_refused(argv):
    with pytest.raises(ValueError):
        rsoverlay.lines_for(argv)
