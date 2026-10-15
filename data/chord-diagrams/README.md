# Chord diagram reconstruction data

These `.rs7da` files and their generator (`tools/diagram_patch.py`) are MIT
licensed under the adjacent LICENSE. They contain our reconstruction operations,
lengths, and SHA-256 digests, not movies or Ubisoft class bytecode. Retained bytes
are copied from the user's own original DoInitAction tag. Even single original
byte values are encoded as COPY operations rather than shipped as literals.
New compiler/extension output is reconstructed at runtime and checked against
its expected digest. The loader replaces one complete tag; all other bytes are
preserved except the movie's declared file length.

Generate from the offline patcher output (`tools/gfxpatch`):

```powershell
# From the repository root; reads the Desktop copies, writes data only in this checkout.
uv run python tools\diagram_patch.py
```

The generator never edits the patcher folder (`tools\gfxpatch`). Its `--original` and
`--edited` options select alternate input roots. All three movies must pass
one-tag and byte-identity checks before any recipe is written. FFDec/Python/Java
are not needed on the user's runtime machine. `build.ps1` copies this folder to
`bin\Release\chord-diagrams`; distribute it alongside the DLL.

Runtime support is deliberately limited to the user's uncompressed GFX/FWS
movies. Compressed movies, unknown original tag hashes, duplicate class tags,
malformed records and already patched movies are refused without a tag swap.
Do not distribute original/edited movies or intermediate class exports.
