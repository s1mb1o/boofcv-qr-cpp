#!/usr/bin/env python3
"""Small Python-side timing helper for boofcv_qr."""

from __future__ import annotations

import argparse
import time
from pathlib import Path

import numpy as np

import boofcv_qr as pb


def profile_single(path: Path, iters: int) -> None:
    detector = pb.FactoryFiducial(np.uint8).qrcode()
    image = pb.load_single_band(path, np.uint8)
    detector.detect(image)

    t0 = time.perf_counter()
    detected = 0
    for _ in range(iters):
        detector.detect(image)
        detected += len(detector.detections)
    total_ms = (time.perf_counter() - t0) * 1000.0
    print(
        f"single: total={total_ms:.1f} ms "
        f"mean={total_ms / iters:.3f} ms detections={detected}"
    )


def profile_batch(paths: list[Path], threads: int, repeats: int) -> None:
    pb.scan_batch(paths, threads=threads)

    t0 = time.perf_counter()
    total_detections = 0
    for _ in range(repeats):
        results = pb.scan_batch(paths, threads=threads)
        total_detections += sum(len(result.detections) for result in results)
    total_ms = (time.perf_counter() - t0) * 1000.0
    images = len(paths) * repeats
    print(
        f"batch: total={total_ms:.1f} ms mean_image={total_ms / images:.3f} ms "
        f"images={images} threads={threads} detections={total_detections}"
    )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("image", type=Path)
    parser.add_argument("--iters", type=int, default=1000)
    parser.add_argument("--batch-size", type=int, default=0)
    parser.add_argument("--threads", type=int, default=0)
    args = parser.parse_args()

    profile_single(args.image, args.iters)
    if args.batch_size > 0:
        paths = [args.image] * args.batch_size
        profile_batch(paths, args.threads, max(1, args.iters // args.batch_size))


if __name__ == "__main__":
    main()
