"""Generate QR PNG fixtures + JSON ground truth for the C++ unit tests.

One-shot: run with the provided venv and commit the generated PNGs +
JSONs under `tests/fixtures/qr/`.

Each generated fixture renders at module pixel size = 4 with a 4-module
quiet zone border, matching BoofCV's `QrCodeGeneratorImage(4)` defaults.

Ground truth JSON format:
    {
        "name": "v1_M_alphanumeric_HELLO",
        "version": 1,
        "error": "M",
        "message": "HELLO",
        "module_pixels": 4,
        "border_modules": 4,
        "image_size": 84,        # in pixels
        "ppCorner": [[x,y]×4],   # CCW from top-left, in image-pixel coords
        "ppRight": [[x,y]×4],
        "ppDown": [[x,y]×4]
    }

Note: the `qrcode` library doesn't expose the chosen mask pattern, so
the C++ tests don't assert mask equality — they assert payload equality
which is mask-independent.
"""
from __future__ import annotations

import argparse
import json
import os
import sys
from pathlib import Path

import qrcode
from qrcode.constants import ERROR_CORRECT_L, ERROR_CORRECT_M, ERROR_CORRECT_Q, ERROR_CORRECT_H

ERROR_LEVELS = {
    "L": ERROR_CORRECT_L,
    "M": ERROR_CORRECT_M,
    "Q": ERROR_CORRECT_Q,
    "H": ERROR_CORRECT_H,
}


def render(version: int, error: str, message: str, *, module_px: int = 4, border_modules: int = 4):
    qr = qrcode.QRCode(
        version=version,
        error_correction=ERROR_LEVELS[error],
        box_size=module_px,
        border=border_modules,
    )
    qr.add_data(message)
    qr.make(fit=False)  # respect explicit version
    img = qr.make_image(fill_color="black", back_color="white").convert("L")
    return img, qr


def finder_corners(border_modules: int, module_px: int, num_modules: int):
    """Return (ppCorner, ppRight, ppDown) in CCW image-pixel coords.

    Each finder is 7×7 modules. After rendering with PIL, pixel (i, j)
    in module coords lands at image-pixel (border + i*box, border + j*box)
    for the top-left corner of that module. The CCW corner ordering for
    the canonical finder square is:
        [0] top-left   = (b + 0*M, b + 0*M)
        [1] top-right  = (b + 7*M, b + 0*M)
        [2] bot-right  = (b + 7*M, b + 7*M)
        [3] bot-left   = (b + 0*M, b + 7*M)
    """
    b = border_modules * module_px
    side = 7 * module_px
    inner = num_modules - 7  # offset in modules for the right/bottom finders

    pp_corner = [
        (b, b),
        (b + side, b),
        (b + side, b + side),
        (b, b + side),
    ]
    # Top-right finder pattern (canonical orientation: rotated so that
    # corner[0] is the inner-top-left of the right pp).
    rb = b + inner * module_px  # right-side finder's top-left x
    pp_right = [
        (rb, b),
        (rb + side, b),
        (rb + side, b + side),
        (rb, b + side),
    ]
    # Bottom-left finder pattern.
    db = b + inner * module_px  # down-side finder's top-left y
    pp_down = [
        (b, db),
        (b + side, db),
        (b + side, db + side),
        (b, db + side),
    ]
    return pp_corner, pp_right, pp_down


def emit(out_dir: Path, name: str, version: int, error: str, message: str):
    img, qr = render(version, error, message)
    img.save(out_dir / f"{name}.png")
    num_modules = qr.modules_count
    border = qr.border
    box = qr.box_size
    image_size = img.size[0]
    ppc, ppr, ppd = finder_corners(border, box, num_modules)
    truth = {
        "name": name,
        "version": version,
        "error": error,
        "message": message,
        "module_pixels": box,
        "border_modules": border,
        "image_size": image_size,
        "num_modules": num_modules,
        "ppCorner": ppc,
        "ppRight": ppr,
        "ppDown": ppd,
    }
    (out_dir / f"{name}.json").write_text(json.dumps(truth, indent=2))

    # Also emit a flat key=value form so the C++ tests can parse without
    # pulling in a JSON dep. One field per line; polygon corners are
    # written as `ppCorner = x0,y0 x1,y1 x2,y2 x3,y3`.
    def fmt_polygon(p):
        return " ".join(f"{x},{y}" for x, y in p)
    lines = [
        f"name = {name}",
        f"version = {version}",
        f"error = {error}",
        f"message = {message}",
        f"module_pixels = {box}",
        f"border_modules = {border}",
        f"image_size = {image_size}",
        f"num_modules = {num_modules}",
        f"ppCorner = {fmt_polygon(ppc)}",
        f"ppRight = {fmt_polygon(ppr)}",
        f"ppDown = {fmt_polygon(ppd)}",
    ]
    (out_dir / f"{name}.txt").write_text("\n".join(lines) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", default="tests/fixtures/qr", type=Path)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)

    fixtures = [
        ("v1_M_numeric_8",         1, "M", "01234567"),
        ("v1_L_numeric_short",     1, "L", "12345"),
        ("v1_H_numeric_short",     1, "H", "1234"),
        ("v2_M_alphanum_HELLO",    2, "M", "HELLO"),
        ("v2_L_alphanum_long",     2, "L", "01234567ABCD"),
        ("v2_M_byte_short",        2, "M", "Pp4/"),
        ("v3_M_mixed",             3, "M", "1235AFefg"),
        ("v5_M_alphanum",          5, "M", "ALPHANUMTESTV5"),
        ("v7_M_alphanum",          7, "M", "VERSION7TEST"),
        ("v10_M_alphanum",         10, "M", "VERSIONTENISGOOD"),
        ("v20_M_alphanum",         20, "M", "VERSIONTWENTYTEST"),
        ("v40_M_numeric",          40, "M", "1234567890" * 5),
    ]

    for name, version, error, message in fixtures:
        try:
            emit(args.out, name, version, error, message)
            print(f"Wrote {name}.png + {name}.json")
        except Exception as e:
            print(f"FAILED {name}: {e}", file=sys.stderr)
            raise


if __name__ == "__main__":
    main()
