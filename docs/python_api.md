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
```

`detect()` accepts `numpy.uint8` grayscale arrays with shape `(height, width)`
or `(height, width, 1)`. Color arrays are rejected deliberately; convert to
grayscale first or call `load_single_band()`.

## Batch Paths

```python
import boofcv_qr as pb

results = pb.scan_batch(["a.png", "b.png"], threads=8)
for result in results:
    if result.error:
        print(result.path, result.error)
        continue
    for qr in result.detections:
        print(result.path, qr.message)
```

`scan_batch()` preserves input order, uses one detector pipeline per worker,
and releases the GIL while scanning. `threads=0` uses hardware concurrency.

## PyBoof Compatibility Scope

Supported QR subset:

- `FactoryFiducial(np.uint8).qrcode(config=None)`
- `ConfigQrCode`
- `QrCodeDetector.detect(image)`, `.detections`, `.failures`,
  `.get_image_type()`
- `QrCode` fields for message, geometry, QR metadata, corrected bytes, raw
  codewords, Reed-Solomon error locations, and block status
- `Point2D`, `Polygon2D`, `load_single_band()`
- `scan_batch()` as a native extension for parallel path scanning

Not supported:

- non-QR PyBoof modules
- JVM-backed BoofCV APIs
- non-GrayU8 image types
- automatic color conversion inside `detect()`
