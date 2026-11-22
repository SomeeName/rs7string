"""rsxr-convert: bring extended-range charts made for earlier versions into the format the mod reads now.

    rsxr-convert.exe FILE_OR_FOLDER [...]        (or drag files or a folder such as dlc onto it)

For every old-format chart it writes <name>_rsxr_p.psarc next to the old package and moves the old one to the
subfolder old-format as <name>_p.disabled.psarc, so the game no longer loads it.
Scanning a folder also renames old files set aside with the earlier ending so the game ignores them.
Official DLC, ordinary customs and charts that are already in the new format are left alone. Nothing is deleted.

The conversion is tools/native_chart.py; this file is only the part a player sees. scripts/package.ps1 builds
the exe from it.
"""

import os
import sys
from pathlib import Path

import native_chart

OLD_FOLDER = "old-format"
SUFFIX, NEW_SUFFIX = "_p.psarc", "_rsxr_p.psarc"
DISABLED_SUFFIX = "_p.disabled.psarc"


def packages(arguments):
    """The Windows packages named by the arguments: files as given, folders searched with their subfolders."""
    found = []
    for argument in arguments:
        path = Path(argument)
        if path.is_dir():
            found += sorted(
                p
                for p in path.rglob("*" + SUFFIX)
                if p.is_file()
                and OLD_FOLDER not in (part.lower() for part in (path.resolve().name, *p.relative_to(path).parts))
            )
        else:
            found.append(path)
    return list(dict.fromkeys(found))


def free_name(path):
    """path, or path with (2), (3)... before the endings when something has that name already."""
    candidate, n = path, 1
    while candidate.exists():
        n += 1
        candidate = path.with_name(f"{path.name[: -len(DISABLED_SUFFIX)]} ({n}){DISABLED_SUFFIX}")
    return candidate


def repair_old_files(arguments):
    """Rename files set aside by earlier converters. Returns (renamed, failed)."""
    found = set()
    for argument in arguments:
        folder = Path(argument)
        if folder.is_dir():
            found.update(
                p for p in folder.rglob("*" + SUFFIX + ".old") if p.is_file() and p.parent.name.lower() == OLD_FOLDER
            )
    renamed, failed = 0, 0
    for path in sorted(found):
        target = path.with_name(path.name[: -len(SUFFIX + ".old")] + DISABLED_SUFFIX)
        try:
            os.replace(path, free_name(target))
            renamed += 1
        except OSError as error:
            failed += 1
            print(f"failed     {path.name}  (could not rename the old file: {error}. Move it out of dlc)")
    return renamed, failed


def convert(path):
    """Convert one package. Returns (state, text): state is 'converted', 'skipped' or 'failed'."""
    name = path.name
    if not path.is_file():
        return "failed", "not found"
    if name.lower().endswith(DISABLED_SUFFIX):
        return "skipped", "set aside earlier (old format)"
    if not name.lower().endswith(SUFFIX):
        return "skipped", "not a Windows song package (_p.psarc)"
    with path.open("rb") as stream:
        header = stream.read(32)
    if len(header) != 32 or header[:4] not in (b"PSAR", b"PSXR"):
        return "failed", "not a song package"
    if header[:4] == b"PSXR" or name.lower().endswith(NEW_SUFFIX):
        return "skipped", "already in the new format"
    if int.from_bytes(header[28:32], "big") & 4:
        return "skipped", "official DLC or an ordinary custom"
    target = path.with_name(name[: -len(SUFFIX)] + NEW_SUFFIX)
    if target.exists():
        return "skipped", f"{target.name} is already there"
    temporary = target.with_name(target.name + ".tmp")
    try:
        native_chart.convert_package(path, temporary, True)
        with temporary.open("rb") as stream:
            extended = stream.read(4) == b"PSXR"
        if not extended:
            return "skipped", "no extended-range arrangement"
        os.replace(temporary, target)
    except (ValueError, OSError) as error:
        return "failed", str(error)
    finally:
        temporary.unlink(missing_ok=True)
    try:
        folder = path.parent / OLD_FOLDER
        folder.mkdir(exist_ok=True)
        os.replace(path, free_name(folder / (name[: -len(SUFFIX)] + DISABLED_SUFFIX)))
    except OSError as error:
        return "failed", f"wrote {target.name}, but could not move the old file away: {error}. Move it out of dlc"
    return "converted", target.name


def main(argv=None):
    arguments = list(sys.argv[1:] if argv is None else argv)
    pause = "--no-pause" not in arguments and sys.stdin is not None and sys.stdin.isatty()
    arguments = [a for a in arguments if a != "--no-pause"]
    for stream in (sys.stdout, sys.stderr):
        if stream is not None and hasattr(stream, "reconfigure"):
            stream.reconfigure(errors="replace")
    counts = {"converted": 0, "skipped": 0, "failed": 0}
    if arguments:
        renamed, counts["failed"] = repair_old_files(arguments)
        for path in packages(arguments):
            state, text = convert(path)
            counts[state] += 1
            print(f"{state:9}  {path.name}" + (f"  ->  {text}" if state == "converted" else f"  ({text})"))
        print(f"\n{counts['converted']} converted, {counts['skipped']} left alone, {counts['failed']} failed.")
        if renamed:
            print(f"{renamed} old files renamed so the game no longer loads them.")
        if counts["converted"]:
            print(f"The old files are in the folder {OLD_FOLDER} next to the new ones.")
    else:
        print(__doc__.split("The conversion is")[0].strip())
    if pause:
        input("\nPress Enter to close.")
    return 1 if counts["failed"] or not arguments else 0


if __name__ == "__main__":
    sys.exit(main())
