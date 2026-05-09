#!/usr/bin/env python3
"""Score a baseline run against the BoofCV qrcodes_v3 ground truth.

Reads `summary.json` produced by `tools/java_reference/Baseline.java`,
matches each detected QR polygon against the ground-truth polygon by IoU,
and aggregates per-category metrics:

  - detection rate          (matches at IoU >= threshold / total ground-truth)
  - precision               (matches / total detections)
  - decode-success rate     (matched detections with non-empty `message`)
  - per-image wall-clock    (mean / p50 / p95)

Writes `<output>/baseline.json` with the per-category breakdown plus an
aggregate row, and prints a human-readable table to stdout.

Usage:
    score.py <summary.json> <output.json> [--iou 0.5]
"""
from __future__ import annotations

import argparse
import json
import statistics
import sys
from collections import defaultdict
from pathlib import Path
from typing import Any

from shapely.geometry import Polygon


def polygon_iou(a: list[list[float]], b: list[list[float]]) -> float:
    """Intersection-over-union of two 4-corner polygons.

    Returns 0 for degenerate / non-overlapping inputs.
    """
    try:
        pa = Polygon(a)
        pb = Polygon(b)
        if not pa.is_valid:
            pa = pa.buffer(0)
        if not pb.is_valid:
            pb = pb.buffer(0)
        if pa.is_empty or pb.is_empty:
            return 0.0
        inter = pa.intersection(pb).area
        union = pa.area + pb.area - inter
        if union <= 0:
            return 0.0
        return inter / union
    except Exception:
        return 0.0


def match_one_to_one(
    gt_polys: list[list[list[float]]],
    det_polys: list[list[list[float]]],
    iou_threshold: float,
) -> list[tuple[int, int, float]]:
    """Greedy one-to-one matching by descending IoU, gated at threshold.

    Returns a list of (gt_index, det_index, iou) for matched pairs.
    """
    if not gt_polys or not det_polys:
        return []

    pairs: list[tuple[float, int, int]] = []
    for gi, gp in enumerate(gt_polys):
        for di, dp in enumerate(det_polys):
            iou = polygon_iou(gp, dp)
            if iou >= iou_threshold:
                pairs.append((iou, gi, di))
    pairs.sort(reverse=True)

    matched: list[tuple[int, int, float]] = []
    used_gt: set[int] = set()
    used_det: set[int] = set()
    for iou, gi, di in pairs:
        if gi in used_gt or di in used_det:
            continue
        matched.append((gi, di, iou))
        used_gt.add(gi)
        used_det.add(di)
    return matched


def percentile(values: list[float], p: float) -> float:
    if not values:
        return 0.0
    s = sorted(values)
    k = (len(s) - 1) * p
    lo, hi = int(k), min(int(k) + 1, len(s) - 1)
    if lo == hi:
        return s[lo]
    return s[lo] * (hi - k) + s[hi] * (k - lo)


def score(summary_path: Path, iou_threshold: float) -> dict[str, Any]:
    with summary_path.open() as f:
        summary = json.load(f)

    records = summary["records"]
    by_cat: dict[str, dict[str, Any]] = defaultdict(
        lambda: {
            "images": 0,
            "gt_count": 0,
            "det_count": 0,
            "matches": 0,
            "decode_success": 0,
            "payload_exact": 0,
            "elapsed_ms": [],
        }
    )

    for rec in records:
        cat = rec.get("category", "")
        bucket = by_cat[cat]
        bucket["images"] += 1
        bucket["elapsed_ms"].append(float(rec.get("elapsed_ms", 0.0)))

        gt = rec.get("ground_truth", []) or []
        dets = rec.get("detections", []) or []
        bucket["det_count"] += len(dets)

        gt_polys = [g["corners"] for g in gt if "corners" in g]
        gt_messages = [g.get("message") for g in gt]

        # Polygon-based path: when the sidecar has corner GT we score by IoU.
        if gt_polys:
            bucket["gt_count"] += len(gt_polys)
            det_polys = [d["corners"] for d in dets]
            for gi, di, _iou in match_one_to_one(
                gt_polys, det_polys, iou_threshold
            ):
                bucket["matches"] += 1
                msg = dets[di].get("message")
                gt_msg = gt_messages[gi] if gi < len(gt_messages) else None
                if msg is not None and msg != "":
                    bucket["decode_success"] += 1
                if gt_msg is not None and msg is not None and msg == gt_msg:
                    bucket["payload_exact"] += 1
        else:
            # Payload-only sidecar (decoding subset): treat each known-payload
            # entry as one GT, count exact-string matches against any detection.
            payload_gts = [m for m in gt_messages if m is not None and m != ""]
            bucket["gt_count"] += len(payload_gts)
            det_msgs = [d.get("message") for d in dets]
            for gt_msg in payload_gts:
                # First detection that matches this GT exactly counts; greedy.
                for j, dm in enumerate(det_msgs):
                    if dm == gt_msg:
                        bucket["matches"] += 1
                        bucket["decode_success"] += 1
                        bucket["payload_exact"] += 1
                        det_msgs[j] = None  # consume
                        break

    out_categories: dict[str, Any] = {}
    agg = {
        "images": 0,
        "gt_count": 0,
        "det_count": 0,
        "matches": 0,
        "decode_success": 0,
        "payload_exact": 0,
        "elapsed_ms": [],
    }
    for cat, b in sorted(by_cat.items()):
        for k in ("images", "gt_count", "det_count", "matches",
                 "decode_success", "payload_exact"):
            agg[k] += b[k]
        agg["elapsed_ms"].extend(b["elapsed_ms"])

        det_rate = b["matches"] / b["gt_count"] if b["gt_count"] else 0.0
        precision = b["matches"] / b["det_count"] if b["det_count"] else 0.0
        decode_rate = b["decode_success"] / b["gt_count"] if b["gt_count"] else 0.0
        payload_rate = (
            b["payload_exact"] / b["gt_count"] if b["gt_count"] else 0.0
        )
        ms = b["elapsed_ms"]
        out_categories[cat] = {
            "images": b["images"],
            "gt_count": b["gt_count"],
            "det_count": b["det_count"],
            "matches_at_iou": b["matches"],
            "decode_success": b["decode_success"],
            "payload_exact": b["payload_exact"],
            "detection_rate": det_rate,
            "precision": precision,
            "decode_rate": decode_rate,
            "payload_exact_rate": payload_rate,
            "elapsed_ms_mean": statistics.fmean(ms) if ms else 0.0,
            "elapsed_ms_p50": percentile(ms, 0.50),
            "elapsed_ms_p95": percentile(ms, 0.95),
        }

    overall_det = agg["matches"] / agg["gt_count"] if agg["gt_count"] else 0.0
    overall_prec = agg["matches"] / agg["det_count"] if agg["det_count"] else 0.0
    overall_decode = (
        agg["decode_success"] / agg["gt_count"] if agg["gt_count"] else 0.0
    )
    overall_payload = (
        agg["payload_exact"] / agg["gt_count"] if agg["gt_count"] else 0.0
    )
    ms_all = agg["elapsed_ms"]

    return {
        "iou_threshold": iou_threshold,
        "boofcv_version": summary.get("boofcv_version"),
        "dataset_root": summary.get("dataset_root"),
        "total_elapsed_ms": summary.get("total_elapsed_ms"),
        "warmup_images": summary.get("warmup_images"),
        "aggregate": {
            "images": agg["images"],
            "gt_count": agg["gt_count"],
            "det_count": agg["det_count"],
            "matches_at_iou": agg["matches"],
            "decode_success": agg["decode_success"],
            "payload_exact": agg["payload_exact"],
            "detection_rate": overall_det,
            "precision": overall_prec,
            "decode_rate": overall_decode,
            "payload_exact_rate": overall_payload,
            "elapsed_ms_mean": statistics.fmean(ms_all) if ms_all else 0.0,
            "elapsed_ms_p50": percentile(ms_all, 0.50),
            "elapsed_ms_p95": percentile(ms_all, 0.95),
        },
        "categories": out_categories,
    }


def print_table(result: dict[str, Any]) -> None:
    header = (
        f"{'category':<14} {'imgs':>5} {'GT':>5} {'det':>5} {'TP':>5} "
        f"{'detRate':>8} {'prec':>6} {'decode':>7} {'payload':>8} "
        f"{'meanMs':>7} {'p50Ms':>7} {'p95Ms':>7}"
    )
    print(header)
    print("-" * len(header))

    def row(name: str, m: dict[str, Any]) -> str:
        return (
            f"{name:<14} {m['images']:>5} {m['gt_count']:>5} {m['det_count']:>5} "
            f"{m['matches_at_iou']:>5} "
            f"{m['detection_rate']*100:>7.2f}% "
            f"{m['precision']*100:>5.1f}% "
            f"{m['decode_rate']*100:>6.2f}% "
            f"{m['payload_exact_rate']*100:>7.2f}% "
            f"{m['elapsed_ms_mean']:>7.2f} "
            f"{m['elapsed_ms_p50']:>7.2f} "
            f"{m['elapsed_ms_p95']:>7.2f}"
        )

    for cat, m in result["categories"].items():
        print(row(cat, m))
    print("-" * len(header))
    print(row("AGGREGATE", result["aggregate"]))


def main(argv: list[str]) -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("summary", type=Path, help="Path to summary.json")
    p.add_argument("output", type=Path, help="Where to write baseline.json")
    p.add_argument("--iou", type=float, default=0.5, help="IoU threshold (default 0.5)")
    args = p.parse_args(argv)

    result = score(args.summary, args.iou)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w") as f:
        json.dump(result, f, indent=2)
    print_table(result)
    print()
    print(f"Wrote {args.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
