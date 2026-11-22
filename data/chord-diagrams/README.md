# Chord diagram reconstruction data

These `.rsxrda` files and their generator (`tools/diagram_patch.py`) are MIT
licensed under the LICENSE file in this folder. They contain our reconstruction
operations, lengths and SHA-256 digests. They do not contain movies or Ubisoft
class bytecode.

At runtime the loader rebuilds the patched tag from these recipes:

- Bytes that stay the same are copied from the user's own original DoInitAction tag.
  Even single original byte values are stored as COPY operations, not shipped as literals.
- New compiler/extension output is rebuilt at runtime and checked against its
  expected digest.
- The loader replaces one complete tag. It keeps all other bytes, except the
  movie's declared file length.

To generate the files, run the generator on the output of the offline patcher (`tools/gfxpatch`):

```powershell
# From the repository root; reads the Desktop copies, writes data only in this checkout.
uv run python tools\diagram_patch.py --edited "$env:USERPROFILE\Desktop\rocksmith-assets\edited\gfx"
```

The generator never edits the patcher folder (`tools\gfxpatch`). Use its `--original` and
`--edited` options to read from other input folders. All three movies must pass the
one-tag and byte-identity checks before the generator writes any recipe.

The user's machine does not need FFDec, Python or Java to run the mod. `build.ps1` copies
this folder to `bin\Release\chord-diagrams`. Ship it next to the DLL.

The runtime supports only the user's uncompressed GFX/FWS movies, on purpose. It refuses
these cases and leaves the tag unchanged:

- compressed movies
- unknown original tag hashes
- duplicate class tags
- malformed records
- movies that are already patched

Do not distribute original or edited movies, or intermediate class exports.
