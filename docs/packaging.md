# Packaging

`boofcv-qr-cpp` ships a native Python extension. The extension links to OpenCV
through CMake, so wheel portability depends on how OpenCV is supplied.

## Supported Python Targets

- CPython 3.10, 3.11, 3.12, 3.13, and 3.14.
- macOS arm64/x86_64 where Homebrew OpenCV is available during wheel build.
- Linux x86_64 source builds or native wheels on systems with compatible
  OpenCV runtime libraries installed.

Windows wheels are not published yet.

## macOS Wheels

Release macOS wheels are built with `cibuildwheel` and repaired with
`delocate`:

```bash
python -m pip install cibuildwheel==3.3.1 delocate
CIBW_PLATFORM=macos python -m cibuildwheel --output-dir dist
```

The release workflow installs Homebrew OpenCV before building, then runs the
Python compatibility smoke test from the cibuildwheel `test-command`.

## Linux Wheels

Linux release artifacts are currently **system-OpenCV wheels**, not manylinux
wheels. They are built on Ubuntu with `libopencv-dev`, inspected with
`auditwheel show`, and uploaded under artifact names containing
`linux-system-opencv`.

This is intentional for now. A true manylinux wheel needs a deliberate OpenCV
bundling strategy: either build OpenCV inside a manylinux image and let
`auditwheel repair` vendor the required shared libraries, or ship a smaller
OpenCV subset. Both options materially increase build time, artifact size, and
license/compliance surface.

Linux users should install OpenCV runtime packages before installing a native
wheel, or build from source in an environment where CMake can find OpenCV:

```bash
sudo apt-get install libopencv-dev
python -m pip install .
```

## Local Smoke

```bash
python3 -m pip wheel . --no-deps -w /tmp/boofcv_qr_dist_check
python3 -m venv /tmp/boofcv_qr_smoke
/tmp/boofcv_qr_smoke/bin/python -m pip install numpy /tmp/boofcv_qr_dist_check/*.whl
BOOFCV_QR_FIXTURE_DIR=tests/fixtures/qr \
  /tmp/boofcv_qr_smoke/bin/python tests/python/test_pyboof_compat.py
```
