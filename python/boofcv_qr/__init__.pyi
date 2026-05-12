from __future__ import annotations

from typing import Any, overload
from os import PathLike

import numpy as np

__version__: str

class Point2D:
    x: float
    y: float
    def __init__(self, x: float = 0.0, y: float = 0.0) -> None: ...
    def get_tuple(self) -> tuple[float, float]: ...
    def as_tuple(self) -> tuple[float, float]: ...
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
    def as_list(self) -> list[tuple[float, float]]: ...
    def side_length(self, side: int) -> float: ...
    def __len__(self) -> int: ...
    def __getitem__(self, index: int) -> Point2D: ...

class QrCodeAlignment:
    pixel: Point2D
    moduleX: int
    moduleY: int
    moduleFound: Point2D
    threshold: float
    def __init__(self) -> None: ...
    def as_dict(self) -> dict[str, Any]: ...

class ImageType:
    family: str
    dtype: str

class ConfigQrCode:
    defaultEncoding: str
    forceEncoding: str | None
    considerTransposed: bool
    ignorePaddingBytes: bool
    def __init__(self) -> None: ...

class BatchScanConfig:
    threads: int
    config: ConfigQrCode
    opencv_threads: int
    def __init__(self) -> None: ...

class QrCode:
    version: int
    message: str
    corrected: np.ndarray[Any, np.dtype[np.uint8]] | None
    corrected_bytes: np.ndarray[Any, np.dtype[np.uint8]] | None
    byteEncoding: str
    byte_encoding: str
    totalBitErrors: int
    total_bit_errors: int
    bitsTransposed: bool
    bits_transposed: bool
    error_level: str
    mask_pattern: str
    mode: str
    failure_cause: str
    bounds: Polygon2D
    corners: list[tuple[float, float]]
    pp_right: Polygon2D
    pp_corner: Polygon2D
    pp_down: Polygon2D
    position_patterns: dict[str, list[tuple[float, float]]]
    threshCorner: float
    threshDown: float
    threshRight: float
    threshDownRight: float
    alignment: list[QrCodeAlignment]
    alignment_patterns: list[QrCodeAlignment]
    rawCodewords: np.ndarray[Any, np.dtype[np.uint8]]
    raw_codewords: np.ndarray[Any, np.dtype[np.uint8]]
    rsErrorLocations: list[int]
    rs_error_locations: list[int]
    blockStatus: list[str]
    block_status: list[str]
    def __init__(self) -> None: ...
    def as_dict(self) -> dict[str, Any]: ...

class ScanResult:
    path: str
    detections: list[QrCode]
    failures: list[QrCode]
    error: str
    elapsed_ms: float
    ok: bool
    def __init__(self) -> None: ...
    def as_dict(self) -> dict[str, Any]: ...

class QrCodeDetector:
    detections: list[QrCode]
    failures: list[QrCode]
    def __init__(self, config: ConfigQrCode = ...) -> None: ...
    def detect(self, image: np.ndarray[Any, np.dtype[np.uint8]]) -> None: ...
    def detect_polygons_only(self, image: np.ndarray[Any, np.dtype[np.uint8]]) -> list[QrCode]: ...
    def get_image_type(self) -> ImageType: ...

class FactoryFiducial:
    def __init__(self, image_type: Any) -> None: ...
    def qrcode(self, config: ConfigQrCode | None = None) -> QrCodeDetector: ...

def load_single_band(path: str | PathLike[str], dtype: Any = None) -> np.ndarray[Any, np.dtype[np.uint8]]: ...
@overload
def scan_batch(
    paths: list[str | PathLike[str]],
    threads: int = 0,
    config: ConfigQrCode | None = None,
) -> list[ScanResult]: ...
@overload
def scan_batch(
    paths: list[str | PathLike[str]],
    batch_config: BatchScanConfig,
) -> list[ScanResult]: ...
