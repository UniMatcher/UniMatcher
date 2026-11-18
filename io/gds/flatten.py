"""Flatten one GDSII cell to BOUNDARY geometry in the original database units."""

import argparse
from pathlib import Path

import klayout.db as kdb


def flatten(source, destination, cell_name=None):
    if source.resolve() == destination.resolve():
        raise ValueError("choose a separate output file")
    layout = kdb.Layout()
    layout.read(str(source))
    if cell_name is None:
        choices = list(layout.top_cells())
        if len(choices) != 1:
            names = ", ".join(c.name for c in choices)
            raise ValueError(f"select a cell with --top (top cells: {names})")
        top = choices[0]
    else:
        top = layout.cell(cell_name)
        if top is None:
            raise ValueError(f"cell {cell_name!r} does not exist")

    flat = kdb.Layout()
    flat.dbu = layout.dbu
    out = flat.create_cell(top.name)
    for layer in layout.layer_indexes():
        target = out.shapes(flat.layer(layout.get_info(layer)))
        for it in top.begin_shapes_rec(layer).each():
            shape = it.shape()
            if shape.is_text():
                continue
            polygon = shape.polygon
            if polygon is None:
                raise ValueError(f"unsupported shape on {layout.get_info(layer)}")
            target.insert(polygon.transformed(it.trans()))
    flat.write(str(destination))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path, help="flat output file with a .gds extension")
    parser.add_argument("--top", help="cell name, required with multiple top cells")
    args = parser.parse_args()
    if args.output.suffix.lower() not in {".gds", ".gds2", ".gdsii"}:
        parser.error("use a GDSII output filename")
    try:
        flatten(args.input, args.output, args.top)
    except (ValueError, RuntimeError) as error:
        parser.exit(1, f"flatten: {error}\n")


if __name__ == "__main__":
    main()
