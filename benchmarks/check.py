#!/usr/bin/env python3
"""Run the released SDK against contest gold, including flexible and multilayer cases."""
import argparse
import lzma
from pathlib import Path
import runpy
import shutil
import subprocess
import tempfile

DATA = Path(__file__).resolve().parent


def unpack(archive, work):
    layout = work / archive.stem
    with lzma.open(archive, "rb") as source, layout.open("wb") as target:
        shutil.copyfileobj(source, target, length=1024 * 1024)
    return layout


def check_2023(cli, case, device, work):
    root = DATA / "eda2023"
    folder = root / ("hidden_case" if case == "hidden" else "public_case")
    filename = "layout_fin" if case == "hidden" else case
    gold = folder / "answer" if case == "hidden" else folder / "answer" / case
    layout = unpack(folder / "layout" / (filename + ".txt.xz"), work)
    patterns = [folder / "lib" / ("lib%d.txt" % i) for i in (1, 2, 3)]
    read_markers = runpy.run_path(str(root / "judge" / "pmJudge.py"))["parse_file"]
    for mode, flags in [("exact", ["--identity"]), ("wi_rot_mir", [])]:
        output = work / mode
        subprocess.run([cli, "2023", "--device", device, *flags, layout, output, *patterns],
                       check=True)
        for i, pattern in enumerate(patterns):
            result = output / ("pattern_%d" % i) / "marker.txt"
            expected = gold / (pattern.stem + "_" + mode + ".txt")
            # The official parser canonicalizes each marker and sorts the list.
            # List equality also catches repeated markers.
            if read_markers(result) != read_markers(expected):
                raise ValueError("Marker mismatch for 2023/%s/%s/%s" % (case, mode, pattern.stem))
        print("PASS 2023/%s/%s (lib1, lib2, lib3)" % (case, mode), flush=True)


def check_2024(cli, case, device, work):
    root = DATA / "eda2024"
    folder = root / "Hidden Case" if case == "hidden" else root / "public" / case
    filename = "small_case" if case == "small" else case + "_layout"
    layout = unpack(folder / (filename + ".txt.xz"), work)
    result = work / "discrepancies.txt"
    subprocess.run([cli, "2024", "--device", device, layout, result, folder / (case + "_pattern.txt")],
                   check=True)
    judge = runpy.run_path(str(root / "judge" / "pmJudge.py"))
    actual = judge["parse_file"](result)
    expected = judge["parse_file"](folder / (case + "_answer.txt"))
    compare = judge["compare_patterns"]
    if not (compare(actual, expected) and compare(expected, actual, "res")):
        raise ValueError("Multilayer or XOR mismatch for 2024/" + case)
    print("PASS 2024/%s (%d patterns)" % (case, len(expected)), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True, help="built unimatcher-contest")
    parser.add_argument("--case", choices=("small", "large", "hidden", "all"), default="small")
    parser.add_argument("--device", default="0", help="CUDA device ordinal")
    args = parser.parse_args()
    cli = args.binary.resolve()
    cases = ("small", "large", "hidden") if args.case == "all" else (args.case,)
    for case in cases:
        for check in (check_2023, check_2024):
            with tempfile.TemporaryDirectory(prefix="unimatcher-") as work:
                check(cli, case, args.device, Path(work))


if __name__ == "__main__":
    main()
