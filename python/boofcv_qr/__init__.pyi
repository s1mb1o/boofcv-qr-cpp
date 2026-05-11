from __future__ import annotations

from typing import Any

import numpy as np

__version__: str

class Point2D:
    x: float
    y: float
    def __init__(self, x: float = 0.0, y: float = 0.0) -> None: ...
    def get_tuple(self) -> tuple[float, float]: ...
    def get_x(self) -> float: ...
    def get_y(self) -> float: ...
    def set_x(self, x: float) -> None: ...
    def set_y(self, y: float) -> None: ...
    def distance(self, point: Point2D) -> float: ...
    def copy(self) -> Point2D: ...

class Polygon2D:
    vertexes: list[Point2D]
    def __init__(self, data: int | list[tuple[float, float]] | None = None) -> None: ...
    def convert_tuple(self) -> list[tuple[float, float]]: ...
    def side_length(self, side: int) -> float: ...

class ImageType:
    family: str
    dtype: str

class ConfigQrCode:
    defaultEncoding: str
    forceEncoding: str | None
    considerTransposed: bool
    ignorePaddingBytes: bool
    def __init__(self) -> None: ...

class QrCode:
    version: int
    message: str
    corrected: np.ndarray[Any, np.dtype[np.uint8]] | None
    byteEncoding: str
    totalBitErrors: int
    bitsTransposed: bool
    error_level: str
    mask_pattern: str
    mode: str
    failure_cause: str
    bounds: Polygon2D
    pp_right: Polygon2D
    pp_corner: Polygon2D
    pp_down: Polygon2D
    rawCodewords: np.ndarray[Any, np.dtype[np.uint8]]
    rsErrorLocations: list[int]
    blockStatus: list[str]
    def __init__(self) -> None: ...

class QrCodeDetector:
    detections: list[QrCode]
    failures: list[QrCode]
    def __init__(self, config: ConfigQrCode = ...) -> None: ...
    def detect(self, image: np.ndarray[Any, np.dtype[np.uint8]]) -> None: ...
    def get_image_type(self) -> ImageType: ...

class FactoryFiducial:
    def __init__(self, image_type: Any) -> None: ...
    def qrcode(self, config: ConfigQrCode | None = None) -> QrCodeDetector: ...

def load_single_band(path: str, dtype: Any = None) -> np.ndarray[Any, np.dtype[np.uint8]]: ...
