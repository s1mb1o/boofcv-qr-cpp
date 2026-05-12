"""PyBoof-compatible QR subset backed by boofcv-qr-cpp."""

from ._boofcv_qr import (  # noqa: F401
    BatchScanConfig,
    ConfigQrCode,
    FactoryFiducial,
    ImageType,
    Point2D,
    Polygon2D,
    QrCode,
    QrCodeAlignment,
    QrCodeDetector,
    ScanResult,
    __version__,
    load_single_band,
    scan_batch,
)

__all__ = [
    "BatchScanConfig",
    "ConfigQrCode",
    "FactoryFiducial",
    "ImageType",
    "Point2D",
    "Polygon2D",
    "QrCode",
    "QrCodeAlignment",
    "QrCodeDetector",
    "ScanResult",
    "__version__",
    "load_single_band",
    "scan_batch",
]
