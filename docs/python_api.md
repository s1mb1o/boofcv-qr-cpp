# Python API

`boofcv_qr` exposes a QR-focused subset shaped after PyBoof. It is intended
for code that wants BoofCV-style QR detection without starting a JVM.

## Install From Source

```bash
python3 -m pip install .
```

The wheel build uses CMake and links against the OpenCV installation found on
the build machine.

## Single Image

```python
import numpy as np
import boofcv_qr as pb

detector = pb.FactoryFiducial(np.uint8).qrcode()
image = pb.load_single_band("image.png", np.uint8)
detector.detect(image)

for qr in detector.detections:
    print(qr.message)
    print(qr.bounds.convert_tuple())
    print(qr.raw_codewords.tolist())
```

`detect()` accepts `numpy.uint8` grayscale arrays with shape `(height, width)`
or `(height, width, 1)`. Color arrays are rejected deliberately; convert to
grayscale first or call `load_single_band()`.

## Batch Paths

```python
import boofcv_qr as pb

batch = pb.BatchScanConfig()
batch.threads = 8
batch.config.considerTransposed = True
batch.max_in_flight_mpix = 64.0
batch.reset_pipeline_mpix = 8.0

results = pb.scan_batch(["a.png", "b.png"], batch)
for result in results:
    if not result.ok:
        print(result.path, result.error)
        continue
    for qr in result.detections:
        print(result.path, qr.as_dict()["message"])
```

`scan_batch()` preserves input order, uses one detector pipeline per worker,
and releases the GIL while scanning. The legacy
`scan_batch(paths, threads=0, config=None)` signature remains supported.
`BatchScanConfig.threads=0` uses hardware concurrency. When multiple image
workers are used, OpenCV internal threads are capped to 1 to avoid
oversubscription. Set `BatchScanConfig.opencv_threads`, or
`BOOFCV_QR_OPENCV_THREADS=N`, to override that process-wide cap for
experiments.

Multi-worker scans also use the same memory controls as the CLI. By default,
up to 64 megapixels may be in flight and worker pipelines release large
scratch buffers after images of at least 8 megapixels. Set
`BatchScanConfig.max_in_flight_mpix` and
`BatchScanConfig.reset_pipeline_mpix` to positive values to override, set either
to `0.0` to disable that control, or leave the default negative value to use
environment/default policy. Environment fallbacks are
`BOOFCV_QR_MAX_IN_FLIGHT_MPIX` / `QR_SCAN_MAX_IN_FLIGHT_MPIX` and
`BOOFCV_QR_RESET_PIPELINE_MPIX` / `QR_SCAN_RESET_PIPELINE_MPIX`.

## Detection-Only Stage

```python
detector = pb.FactoryFiducial(np.uint8).qrcode()
image = pb.load_single_band("image.png", np.uint8)

for candidate in detector.detect_polygons_only(image):
    print(candidate.version)
    print(candidate.bounds.as_list())
    print(candidate.position_patterns["corner"])
```

`detect_polygons_only()` runs binarization, polygon detection, finder-pattern
graphing, version estimation, and alignment localization. It returns `QrCode`
shells with finder/bounds/alignment metadata populated, but does not run bit
sampling, Reed-Solomon correction, or message decoding.

## Result Metadata

`QrCode` keeps the original PyBoof-style camelCase names where they mirror
BoofCV (`byteEncoding`, `totalBitErrors`, `rawCodewords`, `rsErrorLocations`,
`blockStatus`) and also exposes Pythonic aliases (`byte_encoding`,
`total_bit_errors`, `raw_codewords`, `rs_error_locations`, `block_status`).

Useful geometry helpers:

- `Point2D.as_tuple()`
- `Polygon2D.as_list()`, `len(polygon)`, `polygon[index]`
- `QrCode.corners`
- `QrCode.position_patterns`
- `QrCode.alignment_patterns`

`QrCode.as_dict()` and `ScanResult.as_dict()` return nested Python containers
for logging and application-level telemetry.

## PyBoof Compatibility Scope

Supported QR subset:

- `FactoryFiducial(np.uint8).qrcode(config=None)`
- `ConfigQrCode`
- `BatchScanConfig`
- `QrCodeDetector.detect(image)`, `.detections`, `.failures`,
  `.detect_polygons_only(image)`, `.get_image_type()`
- `QrCode` fields for message, geometry, QR metadata, corrected bytes, raw
  codewords, Reed-Solomon error locations, and block status
- `Point2D`, `Polygon2D`, `load_single_band()`
- `scan_batch()` as a native extension for parallel path scanning

Not supported:

- non-QR PyBoof modules
- JVM-backed BoofCV APIs
- non-GrayU8 image types
- automatic color conversion inside `detect()`
