#!/usr/bin/env python3
"""Scan image paths in parallel with boofcv_qr.scan_batch()."""

from __future__ import annotations

import argparse
from pathlib import Path

import boofcv_qr as pb


IMAGE_SUFFIXES = {".bmp", ".jpg", ".jpeg", ".png", ".tif", ".tiff", ".webp"}


def collect_images(root: Path) -> list[Path]:
    if root.is_file():
        return [root]
    return sorted(
        path
        for path in root.rglob("*")
        if path.is_file() and path.suffix.lower() in IMAGE_SUFFIXES
    )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path)
    parser.add_argument("--threads", type=int, default=0)
    args = parser.parse_args()

    images = collect_images(args.input)
    results = pb.scan_batch(images, threads=args.threads)

    for result in results:
        if result.error:
            print(f"{result.path}: ERROR: {result.error}")
            continue
        for qr in result.detections:
            print(f"{result.path}: {qr.message}")


if __name__ == "__main__":
    main()
