#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage:
  BOOFCV_QR_DATASET_ROOT=/path/to/boofcv-qrcodes/qrcodes \
    tools/benchmark_compare.sh [--skip-build] [--cpp-only|--skip-java] [output_dir]

  tools/benchmark_compare.sh --compare before/report.json after/report.json

Runs reproducible C++/Java QR benchmark passes and writes:
  report.md
  report.json
  cpp_serial/{stdout.log,time.log,score.json,output/summary.json}
  cpp_batch/{stdout.log,time.log,score.json,output/summary.json}
  java_boofcv/{stdout.log,time.log,output/summary.json}  (unless skipped)

Environment:
  QR_BENCH_BUILD_DIR=build          CMake build directory.
  QR_BENCH_CPP_THREADS=8           C++ batch worker count.
  QR_BENCH_SKIP_JAVA=1             Skip the Java BoofCV reference run.
  QR_BENCH_SKIP_BUILD=1            Reuse the existing build without invoking CMake.
  QR_BENCH_JAVA_XMX=-Xmx4g         JVM heap flag for the Java run.
  JAVA_BIN=/path/to/java           Java executable override.
  JAVA_HOME=/path/to/jdk           Java home fallback.
USAGE
}

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${QR_BENCH_BUILD_DIR:-${REPO_ROOT}/build}"
CPP_THREADS="${QR_BENCH_CPP_THREADS:-8}"
SKIP_JAVA="${QR_BENCH_SKIP_JAVA:-0}"
SKIP_BUILD="${QR_BENCH_SKIP_BUILD:-0}"
JAVA_XMX="${QR_BENCH_JAVA_XMX:--Xmx4g}"
if [[ "${BUILD_DIR}" != /* ]]; then
  BUILD_DIR="${REPO_ROOT}/${BUILD_DIR}"
fi

compare_reports() {
  python3 - "$1" "$2" <<'PY'
import json
import sys
from pathlib import Path

before_path = Path(sys.argv[1])
after_path = Path(sys.argv[2])

def load(path):
    with path.open() as f:
        return json.load(f)

def runs_by_label(report):
    runs = report.get("runs", [])
    if isinstance(runs, dict):
        return runs
    return {run.get("label", f"run_{i}"): run for i, run in enumerate(runs)}

def score_for(report, run):
    score = run.get("score")
    if score:
        return score
    if run.get("kind") == "java":
        return report.get("locked_java_baseline_score")
    return None

def get_metric(report, run, key):
    if key == "decode_rate":
        score = score_for(report, run) or {}
        return score.get("decode_rate")
    return run.get(key)

def fmt(value, decimals=2):
    if value is None:
        return ""
    return f"{value:.{decimals}f}"

def fmt_delta(delta, decimals=2):
    if delta is None:
        return ""
    sign = "+" if delta >= 0 else ""
    return f"{sign}{delta:.{decimals}f}"

def fmt_pct_delta(before, after):
    if before in (None, 0) or after is None:
        return ""
    delta = (after - before) / before * 100.0
    sign = "+" if delta >= 0 else ""
    return f"{sign}{delta:.1f}%"

def cell(report_before, run_before, report_after, run_after, key, decimals=2, pp=False):
    before = get_metric(report_before, run_before, key)
    after = get_metric(report_after, run_after, key)
    if before is None and after is None:
        return ""
    delta = None if before is None or after is None else after - before
    if pp:
        return (
            f"{fmt(before * 100.0 if before is not None else None, decimals)}% -> "
            f"{fmt(after * 100.0 if after is not None else None, decimals)}% "
            f"({fmt_delta(delta * 100.0 if delta is not None else None, decimals)} pp)"
        )
    pct = fmt_pct_delta(before, after)
    suffix = f", {pct}" if pct else ""
    return f"{fmt(before, decimals)} -> {fmt(after, decimals)} ({fmt_delta(delta, decimals)}{suffix})"

before = load(before_path)
after = load(after_path)
before_runs = runs_by_label(before)
after_runs = runs_by_label(after)
labels = [label for label in before_runs if label in after_runs]
for label in after_runs:
    if label not in before_runs:
        labels.append(label)

print("# QR Benchmark Delta")
print()
print(f"- Before: `{before_path}`")
print(f"- After: `{after_path}`")
if before.get("git_commit") or after.get("git_commit"):
    print(f"- Commits: `{before.get('git_commit', '')}` -> `{after.get('git_commit', '')}`")
if before.get("dataset_root") or after.get("dataset_root"):
    print(f"- Dataset: `{after.get('dataset_root') or before.get('dataset_root')}`")
print()
print("| run | elapsed ms | real s | RSS MiB | footprint MiB | decode rate |")
print("|---|---:|---:|---:|---:|---:|")
for label in labels:
    b = before_runs.get(label, {})
    a = after_runs.get(label, {})
    print(
        f"| `{label}` | "
        f"{cell(before, b, after, a, 'summary_elapsed_ms', 0)} | "
        f"{cell(before, b, after, a, 'real_seconds', 2)} | "
        f"{cell(before, b, after, a, 'rss_mib', 1)} | "
        f"{cell(before, b, after, a, 'peak_footprint_mib', 1)} | "
        f"{cell(before, b, after, a, 'decode_rate', 2, pp=True)} |"
    )
PY
}

OUT_ROOT_ARG="${QR_BENCH_OUTPUT_DIR:-}"
OUT_ROOT_SET=0
COMPARE_BEFORE=""
COMPARE_AFTER=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    -h|--help)
      usage
      exit 0
      ;;
    --skip-build)
      SKIP_BUILD=1
      shift
      ;;
    --cpp-only|--skip-java)
      SKIP_JAVA=1
      shift
      ;;
    --compare)
      if [[ $# -lt 3 ]]; then
        echo "--compare requires two report.json paths." >&2
        usage >&2
        exit 2
      fi
      COMPARE_BEFORE="$2"
      COMPARE_AFTER="$3"
      shift 3
      ;;
    -*)
      echo "Unknown option: $1" >&2
      usage >&2
      exit 2
      ;;
    *)
      if [[ "${OUT_ROOT_SET}" == "1" ]]; then
        echo "Only one output_dir may be provided." >&2
        usage >&2
        exit 2
      fi
      OUT_ROOT_ARG="$1"
      OUT_ROOT_SET=1
      shift
      ;;
  esac
done

if [[ -n "${COMPARE_BEFORE}" || -n "${COMPARE_AFTER}" ]]; then
  if [[ -z "${COMPARE_BEFORE}" || -z "${COMPARE_AFTER}" ]]; then
    echo "--compare requires two report.json paths." >&2
    usage >&2
    exit 2
  fi
  compare_reports "${COMPARE_BEFORE}" "${COMPARE_AFTER}"
  exit 0
fi

if [[ -z "${BOOFCV_QR_DATASET_ROOT:-}" ]]; then
  echo "BOOFCV_QR_DATASET_ROOT is required." >&2
  usage >&2
  exit 2
fi

DATASET_ROOT="$(cd "${BOOFCV_QR_DATASET_ROOT}" && pwd)"
OUT_ROOT="${OUT_ROOT_ARG:-/tmp/boofcv_qr_benchmark_$(date +%Y%m%d_%H%M%S)}"
mkdir -p "${OUT_ROOT}"

if [[ "$(uname -s)" != "Darwin" ]]; then
  echo "This RSS benchmark currently expects macOS /usr/bin/time -l output." >&2
  exit 2
fi

if [[ "${SKIP_BUILD}" != "1" ]]; then
  if [[ ! -f "${BUILD_DIR}/CMakeCache.txt" ]]; then
    cmake -S "${REPO_ROOT}" -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release
  fi
  cmake --build "${BUILD_DIR}" --target qr_scan -- -j
fi

QR_SCAN_BIN="${BUILD_DIR}/qr_scan"
if [[ ! -x "${QR_SCAN_BIN}" ]]; then
  echo "Expected executable not found: ${QR_SCAN_BIN}" >&2
  exit 2
fi

find_java_bin() {
  if [[ -n "${JAVA_BIN:-}" ]]; then
    printf '%s\n' "${JAVA_BIN}"
  elif [[ -n "${JAVA_HOME:-}" && -x "${JAVA_HOME}/bin/java" ]]; then
    printf '%s\n' "${JAVA_HOME}/bin/java"
  elif [[ -x /opt/homebrew/opt/openjdk/bin/java ]]; then
    printf '%s\n' /opt/homebrew/opt/openjdk/bin/java
  else
    command -v java || true
  fi
}

build_java_classpath() {
  python3 - "${REPO_ROOT}" <<'PY'
import os
import sys
from pathlib import Path

repo = Path(sys.argv[1])
script = repo / "tools/java_reference/build/scripts/qr-boofcv-baseline"
baseline_jar = repo / "tools/java_reference/build/libs/qr-boofcv-baseline.jar"
cache = Path.home() / ".gradle/caches/modules-2/files-2.1"

if not script.is_file():
    print(f"missing generated Java launcher: {script}", file=sys.stderr)
    sys.exit(2)
if not baseline_jar.is_file():
    print(f"missing Java baseline jar: {baseline_jar}", file=sys.stderr)
    sys.exit(2)

classpath_line = None
for line in script.read_text().splitlines():
    if line.startswith("CLASSPATH="):
        classpath_line = line.split("=", 1)[1]
        break
if classpath_line is None:
    print(f"cannot find CLASSPATH in {script}", file=sys.stderr)
    sys.exit(2)

jar_by_name = {}
if cache.is_dir():
    for jar in cache.rglob("*.jar"):
        jar_by_name.setdefault(jar.name, jar)

entries = []
missing = []
for raw in classpath_line.split(":"):
    name = Path(raw).name
    if name == "qr-boofcv-baseline.jar":
        entries.append(str(baseline_jar))
        continue
    jar = jar_by_name.get(name)
    if jar is None:
        missing.append(name)
    else:
        entries.append(str(jar))

if missing:
    print("missing Java dependency jars in ~/.gradle:", file=sys.stderr)
    for name in missing:
        print(f"  {name}", file=sys.stderr)
    sys.exit(2)

print(os.pathsep.join(entries))
PY
}

quote_command() {
  printf '%q ' "$@"
  printf '\n'
}

run_timed() {
  local label="$1"
  shift
  local dir="${OUT_ROOT}/${label}"
  mkdir -p "${dir}"
  quote_command "$@" > "${dir}/command.txt"
  echo ">>> ${label}"
  /usr/bin/time -l "$@" > "${dir}/stdout.log" 2> "${dir}/time.log"
}

score_run() {
  local label="$1"
  local dir="${OUT_ROOT}/${label}"
  python3 "${REPO_ROOT}/tests/regression/score.py" \
    "${dir}/output/summary.json" "${dir}/score.json" --iou 0.5 \
    > "${dir}/score.log"
}

(
  cd "${REPO_ROOT}"
  git rev-parse HEAD > "${OUT_ROOT}/git_commit.txt"
  git status --short > "${OUT_ROOT}/git_status.txt"
)
uname -a > "${OUT_ROOT}/uname.txt"
cmake --version > "${OUT_ROOT}/cmake_version.txt"
python3 --version > "${OUT_ROOT}/python_version.txt" 2>&1

run_timed cpp_serial env \
  QR_SCAN_THREADS=1 \
  "${QR_SCAN_BIN}" "${DATASET_ROOT}" "${OUT_ROOT}/cpp_serial/output"
score_run cpp_serial

run_timed cpp_batch env \
  QR_SCAN_THREADS="${CPP_THREADS}" \
  "${QR_SCAN_BIN}" "${DATASET_ROOT}" "${OUT_ROOT}/cpp_batch/output"
score_run cpp_batch

JAVA_RAN=0
if [[ "${SKIP_JAVA}" != "1" ]]; then
  JAVA_CMD="$(find_java_bin)"
  if [[ -z "${JAVA_CMD}" ]]; then
    echo "Java executable not found; set JAVA_BIN or QR_BENCH_SKIP_JAVA=1." >&2
    exit 2
  fi
  "${JAVA_CMD}" --version > "${OUT_ROOT}/java_version.txt" 2>&1 || {
    echo "Java executable failed --version: ${JAVA_CMD}" >&2
    cat "${OUT_ROOT}/java_version.txt" >&2
    exit 2
  }
  JAVA_CP="$(build_java_classpath)"
  printf '%s\n' "${JAVA_CP}" > "${OUT_ROOT}/java_classpath.txt"
  run_timed java_boofcv \
    "${JAVA_CMD}" -Djava.awt.headless=true "${JAVA_XMX}" \
    -cp "${JAVA_CP}" qrboofcv.Baseline \
    "${DATASET_ROOT}" "${OUT_ROOT}/java_boofcv/output"
  JAVA_RAN=1
fi

python3 - "${REPO_ROOT}" "${OUT_ROOT}" "${DATASET_ROOT}" "${CPP_THREADS}" "${JAVA_XMX}" "${JAVA_RAN}" <<'PY'
import json
import re
import sys
from datetime import datetime, timezone
from pathlib import Path

repo = Path(sys.argv[1])
out = Path(sys.argv[2])
dataset = sys.argv[3]
cpp_threads = sys.argv[4]
java_xmx = sys.argv[5]
java_ran = sys.argv[6] == "1"

def read_text(path):
    p = Path(path)
    return p.read_text().strip() if p.is_file() else ""

def parse_time_log(path):
    text = Path(path).read_text()
    rss_bytes = None
    peak_footprint_bytes = None
    real_s = user_s = sys_s = None
    for line in text.splitlines():
        if "maximum resident set size" in line:
            parts = line.strip().split()
            if parts:
                rss_bytes = int(parts[0])
        if "peak memory footprint" in line:
            parts = line.strip().split()
            if parts:
                peak_footprint_bytes = int(parts[0])
        if " real " in f" {line} " and " user " in f" {line} " and " sys" in f" {line}":
            nums = re.findall(r"([0-9]+(?:\.[0-9]+)?)", line)
            if len(nums) >= 3:
                real_s, user_s, sys_s = map(float, nums[:3])
    return {
        "rss_bytes": rss_bytes,
        "rss_mib": None if rss_bytes is None else rss_bytes / 1048576.0,
        "peak_footprint_bytes": peak_footprint_bytes,
        "peak_footprint_mib": None if peak_footprint_bytes is None else peak_footprint_bytes / 1048576.0,
        "real_seconds": real_s,
        "user_seconds": user_s,
        "sys_seconds": sys_s,
    }

def parse_summary(label):
    p = out / label / "output/summary.json"
    if not p.is_file():
        return {}
    data = json.loads(p.read_text())
    return {
        "image_count": data.get("image_count"),
        "warmup_images": data.get("warmup_images"),
        "summary_elapsed_ms": data.get("total_elapsed_ms"),
    }

def parse_score(label):
    p = out / label / "score.json"
    if not p.is_file():
        return None
    return json.loads(p.read_text()).get("aggregate", {})

def run_entry(label, kind, threads=None):
    d = out / label
    entry = {
        "label": label,
        "kind": kind,
        "threads": threads,
        "command": read_text(d / "command.txt"),
    }
    entry.update(parse_time_log(d / "time.log"))
    entry.update(parse_summary(label))
    score = parse_score(label)
    if score is not None:
        entry["score"] = score
    return entry

runs = [
    run_entry("cpp_serial", "cpp", 1),
    run_entry("cpp_batch", "cpp", int(cpp_threads)),
]
if java_ran:
    runs.append(run_entry("java_boofcv", "java", 1))

locked_baseline = json.loads((repo / "tests/baseline.json").read_text()).get("aggregate", {})
report = {
    "generated_at": datetime.now(timezone.utc).isoformat(),
    "dataset_root": dataset,
    "output_root": str(out),
    "git_commit": read_text(out / "git_commit.txt"),
    "git_status": read_text(out / "git_status.txt"),
    "uname": read_text(out / "uname.txt"),
    "cmake_version": read_text(out / "cmake_version.txt").splitlines()[0],
    "python_version": read_text(out / "python_version.txt"),
    "java_version": read_text(out / "java_version.txt"),
    "java_xmx": java_xmx if java_ran else None,
    "locked_java_baseline_score": locked_baseline,
    "runs": runs,
}
(out / "report.json").write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")

def pct(value):
    return "" if value is None else f"{100.0 * value:.2f}%"

def mib(value):
    return "" if value is None else f"{value:.1f}"

def seconds(value):
    return "" if value is None else f"{value:.2f}"

def ms(value):
    return "" if value is None else f"{value}"

lines = []
lines.append("# QR Benchmark Report")
lines.append("")
lines.append(f"- Generated UTC: `{report['generated_at']}`")
lines.append(f"- Dataset: `{dataset}`")
lines.append(f"- Git commit: `{report['git_commit']}`")
if report["git_status"]:
    lines.append("- Git status: dirty")
else:
    lines.append("- Git status: clean")
lines.append(f"- Host: `{report['uname']}`")
if report["java_version"]:
    lines.append("- Java:")
    for line in report["java_version"].splitlines():
        lines.append(f"  - `{line}`")
lines.append("")
lines.append("| run | threads | elapsed ms | real s | RSS MiB | footprint MiB | decode rate | decoded / gt | note |")
lines.append("|---|---:|---:|---:|---:|---:|---:|---:|---|")
for run in runs:
    score = run.get("score")
    note = ""
    if run["kind"] == "java":
        score = locked_baseline
        note = "accuracy from locked Java baseline; timing/RSS from fresh run"
    decoded = ""
    decode_rate = ""
    if score:
        decode_rate = pct(score.get("decode_rate"))
        decoded = f"{score.get('decode_success', '')} / {score.get('gt_count', '')}"
    lines.append(
        f"| `{run['label']}` | {run.get('threads') or ''} | {ms(run.get('summary_elapsed_ms'))} | "
        f"{seconds(run.get('real_seconds'))} | {mib(run.get('rss_mib'))} | "
        f"{mib(run.get('peak_footprint_mib'))} | "
        f"{decode_rate} | {decoded} | {note} |"
    )
lines.append("")
lines.append("## Commands")
lines.append("")
for run in runs:
    lines.append(f"### {run['label']}")
    lines.append("")
    lines.append("```bash")
    lines.append(run["command"])
    lines.append("```")
    lines.append("")
(out / "report.md").write_text("\n".join(lines) + "\n")
print(f"Wrote {out / 'report.md'}")
print(f"Wrote {out / 'report.json'}")
PY
