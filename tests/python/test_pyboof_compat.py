#!/usr/bin/env python3

import os
from pathlib import Path

import numpy as np

import boofcv_qr as pb


def fixture_path(name: str) -> Path:
    root = Path(os.environ.get("BOOFCV_QR_FIXTURE_DIR", "tests/fixtures/qr"))
    return root / name


def main() -> None:
    detector = pb.FactoryFiducial(np.uint8).qrcode()
    image = pb.load_single_band(str(fixture_path("full_v1_L_M000.png")), np.uint8)

    assert image.dtype == np.uint8
    assert image.ndim == 2

    detector.detect(image)

    assert len(detector.detections) == 1
    assert len(detector.failures) == 0

    qr = detector.detections[0]
    assert qr.version == 1
    assert qr.message == "01234567"
    assert qr.error_level == "L"
    assert qr.mask_pattern == "M000"
    assert qr.mode == "NUMERIC"
    assert qr.failure_cause == ""
    assert qr.corrected is not None
    assert qr.corrected.dtype == np.uint8
    assert np.allclose(qr.bounds.convert_tuple()[0], (8.0, 8.0))
    assert str(qr.bounds).startswith("Polygon2D(")

    image_type = detector.get_image_type()
    assert image_type.family == "SINGLE_BAND"
    assert image_type.dtype == "uint8"

    config = pb.ConfigQrCode()
    config.considerTransposed = False
    detector = pb.FactoryFiducial(np.uint8).qrcode(config)
    detector.detect(image)
    assert len(detector.detections) == 1

    try:
        pb.FactoryFiducial(np.float32).qrcode()
    except TypeError:
        pass
    else:
        raise AssertionError("FactoryFiducial should reject non-uint8 image types")


if __name__ == "__main__":
    main()
