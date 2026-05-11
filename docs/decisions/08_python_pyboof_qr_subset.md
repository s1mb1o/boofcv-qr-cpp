# Decision 08: PyBoof-Compatible Python QR Subset

Date: 2026-05-11

## Context

The project now needs in-process Python use while preserving the C++ port's
BoofCV QR behavior. PyBoof's public QR examples construct detectors through
`FactoryFiducial(np.uint8).qrcode()`, load GrayU8 images with
`load_single_band()`, call `detect()`, and read `detections` / `failures`.

## Options

1. Keep Python users on the `qr_scan` CLI JSON output.
   - Lowest maintenance.
   - Does not satisfy in-process Python use or PyBoof-style migration.
2. Publish as `pyboof` and emulate the full PyBoof package.
   - Best drop-in import name.
   - Misleading because this project only implements the QR subset and does
     not expose the JVM-backed breadth of PyBoof.
3. Publish as `boofcv_qr` with a PyBoof-compatible QR subset.
   - Honest about scope.
   - Keeps existing PyBoof QR code structurally familiar with
     `import boofcv_qr as pb`.
   - Leaves room for Python-specific packaging without renaming the C++ target.

## Decision

Use option 3. Add a pybind11 extension package named `boofcv_qr` that mirrors
the PyBoof QR subset:

- `FactoryFiducial(np.uint8).qrcode(config=None)`
- `ConfigQrCode`
- `QrCodeDetector.detect(image)`, `.detections`, `.failures`,
  `.get_image_type()`
- `QrCode` result fields for message, geometry, QR metadata, corrected bytes,
  raw codewords, RS error locations, and block status
- `Point2D`, `Polygon2D`, and `load_single_band()`

Only GrayU8 / `numpy.uint8` input is supported because the ported QR pipeline
is GrayU8-only.

## Consequences

- Python users write `import boofcv_qr as pb` rather than `import pyboof as pb`.
- The package intentionally does not expose unrelated BoofCV/PyBoof modules.
- CMake must build position-independent `boofcv_qr` objects so the extension
  can link the static library.
- CI needs Python development headers and NumPy in addition to the C++ build
  dependencies.
