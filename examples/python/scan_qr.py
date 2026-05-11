#!/usr/bin/env python3
"""Scan one image with the PyBoof-compatible boofcv_qr API."""

from __future__ import annotations

import argparse
from pathlib import Path

import numpy as np

import boofcv_qr as pb


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("image", type=Path)
    args = parser.parse_args()

    detector = pb.FactoryFiducial(np.uint8).qrcode()
    image = pb.load_single_band(args.image, np.uint8)
    detector.detect(image)

    for qr in detector.detections:
        print(qr.message)
        print(qr.bounds.convert_tuple())


if __name__ == "__main__":
    main()
