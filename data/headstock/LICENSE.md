MIT License

Copyright (c) 2026 the rs7string authors (owner: SomeeName)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

---

## Scope

This licence covers rs7string_headstock.bin's authored geometry, node edits,
copy/insert instructions and the six procedural rs7hs_*.dds textures, including
their copies in the package. It does not license the user's input templates.

## Not covered: the built `.nif` files

The C++ builder writes each `.nif` against the user's **own installed game NIF**,
which it uses as a structural template. Each `.nif` therefore contains template
data from the game: material and shader settings, extra-data names and values,
and exporter user-property strings. That template data is not ours to license, so the
`.nif` files are **not** covered by this licence and **must not be
redistributed**. Each user rebuilds them from their own game files.
