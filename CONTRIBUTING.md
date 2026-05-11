# Contributing

This project is a fidelity-oriented C++ port of BoofCV's QR detector/decoder.
Changes should preserve BoofCV behavior unless a documented parity decision says
otherwise.

## Local Checks

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target boofcv_qr qr_scan boofcv_qr_tests boofcv_qr_python -- -j
ctest --test-dir build --output-on-failure
```

For the package build:

```bash
python3 -m pip wheel . -w /tmp/boofcv_qr_wheel
```

For Python API edits:

```bash
PYTHONPATH=build/python python3 tests/python/test_pyboof_compat.py
PYTHONPATH=build/python python3 tools/python/profile_python.py \
  tests/fixtures/qr/full_v1_L_M000.png --iters 1000 --batch-size 32 --threads 8
```

If you have the BoofCV QR regression dataset locally:

```bash
BOOFCV_QR_DATASET_ROOT=/path/to/boofcv-qrcodes/qrcodes \
  bash tools/cli/run_regression.sh
```

## Porting Rules

- Keep algorithmic code close to the upstream Java structure.
- Do not replace BoofCV QR internals with OpenCV QR detector APIs.
- Keep OpenCV at the image/math boundary, not as a substitute for QR logic.
- Add or update the companion `src/**/*.md` algorithm note for non-trivial
  algorithmic edits.
- Update `ChangeLog.md` for meaningful changes.
- Record performance or research findings in `ResearchLog.md`.

## License

By contributing, you agree that your contribution is licensed under Apache-2.0.
