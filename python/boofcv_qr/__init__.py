"""PyBoof-compatible QR subset backed by boofcv-qr-cpp."""

from ._boofcv_qr import (  # noqa: F401
    ConfigQrCode,
    FactoryFiducial,
    ImageType,
    Point2D,
    Polygon2D,
    QrCode,
    QrCodeDetector,
    __version__,
    load_single_band,
)

__all__ = [
    "ConfigQrCode",
    "FactoryFiducial",
    "ImageType",
    "Point2D",
    "Polygon2D",
    "QrCode",
    "QrCodeDetector",
    "__version__",
    "load_single_band",
]
