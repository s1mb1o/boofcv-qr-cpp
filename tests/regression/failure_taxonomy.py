#!/usr/bin/env python3
"""Build an image-level failure taxonomy from a QR regression summary.

The input is the `summary.json` emitted by `qr_scan` or the Java reference
runner. The primary taxonomy is C++ misses against ground truth. With
`--java-summary`, the report also highlights Java/C++ decode count drift per
image for diagnostics; that comparison is not itself a requirement to make C++
match Java.

Usage:
    failure_taxonomy.py tests/regression/baseline_cpp/summary.json \
        --java-summary tests/regression/baseline_java/summary.json \
        --output /tmp/qr_failure_taxonomy.json
"""
from __future__ import annotations

import argparse
import json
import sys
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any

from score import match_one_to_one


def load_records(path: Path) -> dict[str, dict[str, Any]]:
    with path.open() as f:
        summary = json.load(f)
    return {rec["image_path"]: rec for rec in summary.get("records", [])}


def decode_stats(rec: dict[str, Any], iou_threshold: float) -> dict[str, Any]:
    gt = rec.get("ground_truth", []) or []
    dets = rec.get("detections", []) or []
    failures = rec.get("failures", []) or []

    gt_polys = [(i, g["corners"]) for i, g in enumerate(gt) if "corners" in g]
    det_polys = [(i, d["corners"]) for i, d in enumerate(dets) if "corners" in d]

    if gt_polys:
        gt_corners = [p for _, p in gt_polys]
        det_corners = [p for _, p in det_polys]
        raw_matches = match_one_to_one(gt_corners, det_corners, iou_threshold)
        matches = [
            {
                "gt_index": gt_polys[gi][0],
                "det_index": det_polys[di][0],
                "iou": iou,
            }
            for gi, di, iou in raw_matches
        ]
        matched_det = {m["det_index"] for m in matches}
        decode_success = sum(
            1 for m in matches if dets[m["det_index"]].get("message") not in (None, "")
        )
        matched_decode_failures = [
            dets[m["det_index"]].get("failure_cause") or "EMPTY_MESSAGE"
            for m in matches
            if dets[m["det_index"]].get("message") in (None, "")
        ]
        return {
            "gt_count": len(gt_polys),
            "det_count": len(dets),
            "failure_count": len(failures),
            "matches": len(matches),
            "decode_success": decode_success,
            "matched_decode_failures": matched_decode_failures,
            "unmatched_gt": len(gt_polys) - len(matches),
            "unmatched_det": len(dets) - len(matched_det),
        }

    gt_messages = [
        g.get("message") for g in gt if g.get("message") not in (None, "")
    ]
    det_messages = [d.get("message") for d in dets]
    payload_matches = 0
    for gt_msg in gt_messages:
        for j, det_msg in enumerate(det_messages):
            if det_msg == gt_msg:
                payload_matches += 1
                det_messages[j] = None
                break

    return {
        "gt_count": len(gt_messages),
        "det_count": len(dets),
        "failure_count": len(failures),
        "matches": payload_matches,
        "decode_success": payload_matches,
        "matched_decode_failures": [],
        "unmatched_gt": len(gt_messages) - payload_matches,
        "unmatched_det": len(dets) - payload_matches,
    }


def failure_causes(items: list[dict[str, Any]]) -> list[str]:
    causes = sorted(
        {
            item.get("failure_cause") or "UNKNOWN"
            for item in items
            if item.get("failure_cause") not in (None, "", "NONE")
        }
    )
    return causes or ["UNKNOWN"]


def classify_cpp_stage(rec: dict[str, Any], stats: dict[str, Any]) -> str:
    gt_count = stats["gt_count"]
    missed = max(0, gt_count - stats["decode_success"])
    if gt_count == 0:
        return "false_positive" if stats["det_count"] else "no_ground_truth"
    if missed == 0:
        return "ok"

    if stats["matched_decode_failures"]:
        causes = sorted(set(stats["matched_decode_failures"]))
        return "matched_decode_failure:" + "+".join(causes)

    failures = rec.get("failures", []) or []
    if failures:
        return "decoder_failure:" + "+".join(failure_causes(failures))

    if stats["det_count"] == 0:
        return "no_decoder_candidate"
    if stats["matches"] == 0:
        return "localization_miss"
    return "partial_decode"


def classify_parity(cpp_stats: dict[str, Any], java_stats: dict[str, Any] | None) -> str:
    if java_stats is None:
        return "no_java_reference"
    cpp = cpp_stats["decode_success"]
    java = java_stats["decode_success"]
    if cpp == java:
        return "same"
    if cpp == 0 and java > 0:
        return "java_positive_cpp_negative"
    if cpp > 0 and java == 0:
        return "cpp_positive_java_negative"
    if cpp < java:
        return "cpp_lower_than_java"
    return "cpp_higher_than_java"


def build_taxonomy(
    cpp_summary: Path,
    java_summary: Path | None,
    iou_threshold: float,
    include_ok: bool,
) -> dict[str, Any]:
    cpp_records = load_records(cpp_summary)
    java_records = load_records(java_summary) if java_summary else {}

    categories: dict[str, dict[str, Any]] = defaultdict(
        lambda: {
            "image_count": 0,
            "gt_count": 0,
            "cpp_decode_success": 0,
            "java_decode_success": 0,
            "cpp_missed_gt": 0,
            "stage_counts": Counter(),
            "parity_counts": Counter(),
            "images": [],
        }
    )
    aggregate_stage = Counter()
    aggregate_parity = Counter()
    image_rows: list[dict[str, Any]] = []
    total_gt = 0
    total_cpp = 0
    total_java = 0

    for image_path in sorted(cpp_records):
        cpp_rec = cpp_records[image_path]
        java_rec = java_records.get(image_path)
        cpp_stats = decode_stats(cpp_rec, iou_threshold)
        java_stats = decode_stats(java_rec, iou_threshold) if java_rec else None
        stage = classify_cpp_stage(cpp_rec, cpp_stats)
        parity = classify_parity(cpp_stats, java_stats)

        java_decode = java_stats["decode_success"] if java_stats else 0
        missed = max(0, cpp_stats["gt_count"] - cpp_stats["decode_success"])
        row = {
            "image_path": image_path,
            "subset": cpp_rec.get("subset", ""),
            "category": cpp_rec.get("category", ""),
            "gt_count": cpp_stats["gt_count"],
            "cpp_decode_success": cpp_stats["decode_success"],
            "java_decode_success": java_decode,
            "cpp_missed_gt": missed,
            "cpp_detections": cpp_stats["det_count"],
            "cpp_failures": cpp_stats["failure_count"],
            "cpp_stage": stage,
            "parity": parity,
        }

        cat = row["category"]
        bucket = categories[cat]
        bucket["image_count"] += 1
        bucket["gt_count"] += cpp_stats["gt_count"]
        bucket["cpp_decode_success"] += cpp_stats["decode_success"]
        bucket["java_decode_success"] += java_decode
        bucket["cpp_missed_gt"] += missed
        bucket["stage_counts"][stage] += 1
        bucket["parity_counts"][parity] += 1
        if include_ok or stage != "ok" or parity != "same":
            bucket["images"].append(row)
            image_rows.append(row)

        aggregate_stage[stage] += 1
        aggregate_parity[parity] += 1
        total_gt += cpp_stats["gt_count"]
        total_cpp += cpp_stats["decode_success"]
        total_java += java_decode

    out_categories: dict[str, Any] = {}
    for cat, bucket in sorted(categories.items()):
        gt_count = bucket["gt_count"]
        cpp_rate = bucket["cpp_decode_success"] / gt_count if gt_count else 0.0
        java_rate = bucket["java_decode_success"] / gt_count if gt_count else 0.0
        out_categories[cat] = {
            "images": bucket["image_count"],
            "gt_count": gt_count,
            "cpp_decode_success": bucket["cpp_decode_success"],
            "java_decode_success": bucket["java_decode_success"],
            "cpp_missed_gt": bucket["cpp_missed_gt"],
            "cpp_decode_rate": cpp_rate,
            "java_decode_rate": java_rate,
            "delta_pp_cpp_minus_java": (cpp_rate - java_rate) * 100.0,
            "stage_counts": dict(sorted(bucket["stage_counts"].items())),
            "parity_counts": dict(sorted(bucket["parity_counts"].items())),
            "notable_images": bucket["images"],
        }

    cpp_rate = total_cpp / total_gt if total_gt else 0.0
    java_rate = total_java / total_gt if total_gt else 0.0
    return {
        "iou_threshold": iou_threshold,
        "cpp_summary": str(cpp_summary),
        "java_summary": str(java_summary) if java_summary else None,
        "aggregate": {
            "images": len(cpp_records),
            "gt_count": total_gt,
            "cpp_decode_success": total_cpp,
            "java_decode_success": total_java,
            "cpp_missed_gt": max(0, total_gt - total_cpp),
            "cpp_decode_rate": cpp_rate,
            "java_decode_rate": java_rate,
            "delta_pp_cpp_minus_java": (cpp_rate - java_rate) * 100.0,
            "stage_counts": dict(sorted(aggregate_stage.items())),
            "parity_counts": dict(sorted(aggregate_parity.items())),
        },
        "categories": out_categories,
        "notable_images": image_rows,
    }


def top_counter(counter: dict[str, int]) -> str:
    non_ok = [(k, v) for k, v in counter.items() if k not in ("ok", "same")]
    if not non_ok:
        return "ok"
    name, count = max(non_ok, key=lambda item: item[1])
    return f"{name} ({count})"


def print_report(taxonomy: dict[str, Any], limit: int) -> None:
    print("C++ accuracy failure taxonomy")
    print(f"IoU threshold: {taxonomy['iou_threshold']}")
    print()
    header = (
        f"{'category':<16} {'GT':>5} {'C++':>5} {'Java':>5} "
        f"{'delta':>9} {'miss':>5} {'top C++ stage':<34} {'top parity drift'}"
    )
    print(header)
    print("-" * len(header))
    for cat, bucket in taxonomy["categories"].items():
        print(
            f"{cat:<16} {bucket['gt_count']:>5} "
            f"{bucket['cpp_decode_success']:>5} "
            f"{bucket['java_decode_success']:>5} "
            f"{bucket['delta_pp_cpp_minus_java']:+8.2f}pp "
            f"{bucket['cpp_missed_gt']:>5} "
            f"{top_counter(bucket['stage_counts']):<34} "
            f"{top_counter(bucket['parity_counts'])}"
        )
    print("-" * len(header))
    agg = taxonomy["aggregate"]
    print(
        f"{'AGGREGATE':<16} {agg['gt_count']:>5} {agg['cpp_decode_success']:>5} "
        f"{agg['java_decode_success']:>5} {agg['delta_pp_cpp_minus_java']:+8.2f}pp "
        f"{agg['cpp_missed_gt']:>5} {top_counter(agg['stage_counts']):<34} "
        f"{top_counter(agg['parity_counts'])}"
    )

    drift_rows = [
        row
        for row in taxonomy["notable_images"]
        if row["parity"] not in ("same", "no_java_reference")
    ]
    drift_rows.sort(
        key=lambda row: (
            row["cpp_decode_success"] - row["java_decode_success"],
            row["category"],
            row["image_path"],
        )
    )
    if drift_rows:
        print()
        print(f"Top Java/C++ parity drifts (limit {limit})")
        for row in drift_rows[:limit]:
            delta = row["cpp_decode_success"] - row["java_decode_success"]
            print(
                f"- {row['image_path']}: cpp={row['cpp_decode_success']} "
                f"java={row['java_decode_success']} delta={delta:+d} "
                f"stage={row['cpp_stage']} parity={row['parity']}"
            )


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("cpp_summary", type=Path, help="C++ summary.json")
    parser.add_argument(
        "--java-summary",
        type=Path,
        help="Optional Java reference summary.json",
    )
    parser.add_argument(
        "--output",
        type=Path,
        help="Optional path for the JSON taxonomy report",
    )
    parser.add_argument("--iou", type=float, default=0.5, help="IoU threshold")
    parser.add_argument(
        "--include-ok",
        action="store_true",
        help="Include OK/same images in the per-category notable image lists",
    )
    parser.add_argument(
        "--limit",
        type=int,
        default=20,
        help="Maximum parity-drift images printed to stdout",
    )
    args = parser.parse_args(argv)

    taxonomy = build_taxonomy(
        args.cpp_summary,
        args.java_summary,
        args.iou,
        include_ok=args.include_ok,
    )
    print_report(taxonomy, args.limit)

    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        with args.output.open("w") as f:
            json.dump(taxonomy, f, indent=2)
        print()
        print(f"Wrote {args.output}")

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
