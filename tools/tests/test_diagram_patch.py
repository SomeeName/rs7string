import importlib.util
from pathlib import Path
import pytest

REPO = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("diagram_patch", REPO / "tools" / "diagram_patch.py")
patch = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patch)


def test_recipe_reconstructs_without_shipping_retained_bytes():
    old = bytes(range(256)) * 4
    new = old[:133] + b"OUR MIT EXTENSION" + old[151:]
    recipe = patch.make_recipe(old, new)
    assert patch.apply_recipe(old, recipe) == new
    assert old[:100] not in recipe
    with pytest.raises(ValueError):
        patch.apply_recipe(b"wrong", recipe)
    with pytest.raises(ValueError):
        patch.apply_recipe(old, recipe[:-1])


@pytest.mark.parametrize("path,sprite", patch.TARGETS.items())
def test_owner_movies_match_offline_patcher(path, sprite):
    root = Path.home() / "Desktop" / "rocksmith-assets"
    src, dst = root / "static.psarc" / "gfxassets" / path, root / "edited2" / "gfx" / path
    if not src.exists() or not dst.exists():
        pytest.skip("owner movie copies absent")
    old, new = src.read_bytes(), dst.read_bytes()
    recipe = patch.movie_recipe(old, new, sprite)
    assert patch.swap_movie(old, recipe, sprite) == new
    assert patch.swap_movie(new, recipe, sprite) is None
    assert patch.swap_movie(old, recipe, sprite + 1) is None
    a, b = patch.tag_span(old, sprite)
    # The distribution contains only reconstruction instructions/hashes, never
    # literals copied from the original Ubisoft tag, even short compiler bytes.
    pos = 84
    for _ in range(int.from_bytes(recipe[16:20], "little")):
        op = recipe[pos]
        pos += 1
        if op == 0:
            pos += 8
        else:
            n = int.from_bytes(recipe[pos : pos + 4], "little")
            pos += 4
            assert set(recipe[pos : pos + n]).isdisjoint(old[a:b])
            pos += n
    assert pos == len(recipe)
