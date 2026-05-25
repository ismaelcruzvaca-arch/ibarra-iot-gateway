#!/usr/bin/env python3
"""
rknn_convert.py — Convert LPRNet ONNX model to RKNN INT8 format.

Platform-specific tool: requires rknn-toolkit2 (Python 3.6-3.11, Ubuntu x86_64).
Runs in the Docker build environment, NOT on the development machine.

Usage (Docker build environment):
    python rknn_convert.py --onnx lprnet.onnx --rknn ocr.rknn \\
                           --dataset calibration.txt --target rv1106

The calibration dataset file lists paths to PNG images (one per line) used
for INT8 quantization. Approximately 100 images from the training set are
recommended.

Example calibration.txt:
    ./calibration/000_000001.png
    ./calibration/000_000002.png
    ...

Output: ocr.rknn (~500KB INT8 quantized), ready for rknn_init() on RV1106.
"""

import argparse
import os
import sys
from typing import List, Optional


def convert(
    onnx_path: str,
    rknn_path: str,
    dataset_path: str,
    target: str = "rv1106",
) -> None:
    """Convert ONNX model to RKNN INT8 quantized format.

    Args:
        onnx_path: Path to input ONNX model (with LogSoftmax included).
        rknn_path: Path for output .rknn file.
        dataset_path: Path to calibration dataset text file (one image path per line).
        target: Target Rockchip platform (rv1106, rv1126, rk3588, etc.).
    """
    try:
        from rknn.api import RKNN
    except ImportError:
        print(
            "[ERROR] rknn-toolkit2 is not installed.",
            file=sys.stderr,
        )
        print(
            "  Install: pip install rknn-toolkit2>=1.6.0",
            file=sys.stderr,
        )
        print(
            "  Requires Python 3.6-3.11 on Ubuntu 18.04/20.04/22.04 x86_64.",
            file=sys.stderr,
        )
        sys.exit(1)

    print(f"[rknn] Converting ONNX -> RKNN INT8")
    print(f"       ONNX       : {onnx_path}")
    print(f"       RKNN       : {rknn_path}")
    print(f"       Calibration: {dataset_path}")
    print(f"       Target     : {target}")
    print()

    # Validate inputs
    if not os.path.isfile(onnx_path):
        print(f"[ERROR] ONNX model not found: {onnx_path}", file=sys.stderr)
        sys.exit(1)
    if not os.path.isfile(dataset_path):
        print(f"[ERROR] Calibration dataset not found: {dataset_path}", file=sys.stderr)
        sys.exit(1)

    # ---- Initialize RKNN context -----------------------------------------
    rknn = RKNN(verbose=False)
    print("[rknn] RKNN context initialized")

    # ---- Configure quantization & platform --------------------------------
    print("[rknn] Configuring ...")
    ret = rknn.config(
        mean_values=[128, 128, 128],
        std_values=[128, 128, 128],
        target_platform=target,
        quantized_algorithm="normal",
        quantized_method="channel",
        optimization_level=3,
    )
    if ret != 0:
        print(f"[ERROR] rknn.config() failed with code {ret}", file=sys.stderr)
        rknn.release()
        sys.exit(1)
    print("[rknn] Configuration OK")

    # ---- Load ONNX model --------------------------------------------------
    print("[rknn] Loading ONNX model ...")
    ret = rknn.load_onnx(onnx_path)
    if ret != 0:
        print(f"[ERROR] rknn.load_onnx() failed with code {ret}", file=sys.stderr)
        rknn.release()
        sys.exit(1)
    print("[rknn] ONNX loaded OK")

    # ---- Build with INT8 quantization ------------------------------------
    print("[rknn] Building RKNN (INT8 quantization) ...")
    ret = rknn.build(do_quantization=True, dataset=dataset_path)
    if ret != 0:
        print(f"[ERROR] rknn.build() failed with code {ret}", file=sys.stderr)
        rknn.release()
        sys.exit(1)
    print("[rknn] Build OK")

    # ---- Export .rknn file ------------------------------------------------
    print(f"[rknn] Exporting to {rknn_path} ...")
    ret = rknn.export_rknn(rknn_path)
    if ret != 0:
        print(f"[ERROR] rknn.export_rknn() failed with code {ret}", file=sys.stderr)
        rknn.release()
        sys.exit(1)

    # Verify output exists
    if os.path.isfile(rknn_path):
        size_kb = os.path.getsize(rknn_path) / 1024.0
        print(f"[rknn] Export OK — {rknn_path} ({size_kb:.1f} KB)")
    else:
        print(f"[WARN] Export completed but {rknn_path} not found", file=sys.stderr)

    # ---- Release context --------------------------------------------------
    rknn.release()
    print("[rknn] Done.")


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def parse_args(argv: Optional[List[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Convert LPRNet ONNX model to RKNN INT8 format.",
    )
    parser.add_argument(
        "--onnx",
        required=True,
        help="Path to input ONNX model (with LogSoftmax included)",
    )
    parser.add_argument(
        "--rknn",
        default="ocr.rknn",
        help="Output .rknn file path (default: ocr.rknn)",
    )
    parser.add_argument(
        "--dataset",
        default="./calibration.txt",
        help="Calibration dataset text file (one image path per line)",
    )
    parser.add_argument(
        "--target",
        default="rv1106",
        choices=["rv1106", "rv1103", "rv1126", "rk3588", "rk3566", "rk3568"],
        help="Target Rockchip platform (default: rv1106)",
    )
    return parser.parse_args(argv)


def main(argv: Optional[List[str]] = None) -> None:
    args = parse_args(argv)
    convert(
        onnx_path=args.onnx,
        rknn_path=args.rknn,
        dataset_path=args.dataset,
        target=args.target,
    )


if __name__ == "__main__":
    main()
