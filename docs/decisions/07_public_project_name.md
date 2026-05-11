# ADR 07: Publish as `boofcv-qr-cpp`

**Date:** 2026-05-11
**Status:** Accepted

## Context

The project is ready to publish as a standalone GitHub repository. The local
directory name was `qr-boofcv-cpp`, but public discoverability matters more than
preserving the local word order.

## Options

1. `qr-boofcv-cpp`
   - Matches the original local directory.
   - Less searchable because the upstream project name is in the middle.

2. `boofcv-qr-cpp`
   - Leads with the upstream project name.
   - Names the exact module scope: QR detector/decoder, not all of BoofCV.
   - Ends with the implementation language/runtime marker used by comparable
     ports.

3. `boofcv-qrcode-cpp`
   - More explicit than `qr`.
   - Slightly longer and less aligned with the existing `boofcv_qr` target and
     include namespace.

## Decision

Use `boofcv-qr-cpp` as the GitHub repository name and public project name.

The CMake/library target remains `boofcv_qr` because it is a valid CMake target
and maps cleanly to C++ include paths.

## Consequences

- Public docs, CMake metadata, NOTICE, and README use `boofcv-qr-cpp`.
- Existing include paths under `include/boofcv_qr/` stay unchanged.
- This repository must state clearly that it is an unofficial port and not an
  official BoofCV project.
