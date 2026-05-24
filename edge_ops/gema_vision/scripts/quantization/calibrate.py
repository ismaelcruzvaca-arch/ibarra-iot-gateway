#!/usr/bin/env python3
"""
calibrate.py  —  RKNN Quantization Calibration Tool
══════════════════════════════════════════════════════════════════════════════

Converts an ONNX model to a Rockchip NPU `.rknn` binary, with INT8
quantization calibrated against a dataset of real production images.

Pipeline
────────
    ONNX (*.onnx)  ──▶  RKNN (*.rknn)
                             │
                     do_quantization=True
                     dataset.txt  (one image path per line)

Target
──────
    Rockchip RV1106 / RV1103  (ARM Cortex-A7 + NPU)

Usage
─────
    python3 calibrate.py              \\
        --onnx    ./model/yolov5n.onnx  \\
        --dataset ./dataset/dataset.txt \\
        --output  ./model/yolov5n.rknn

Docker
──────
    docker build -f Dockerfile.calibrate \\
                  -t gema-calibrate       \\
                  ../../..
    docker run --rm -v $(pwd):/workspace gema-calibrate
"""

from __future__ import annotations

import argparse
import logging
import sys
from pathlib import Path

# ---------------------------------------------------------------------------
# Logging
# ---------------------------------------------------------------------------

logging.basicConfig(
    level=logging.INFO,
    format="[%(asctime)s] %(levelname)-8s %(message)s",
    datefmt="%H:%M:%S",
)
log = logging.getLogger("calibrate")

# ---------------------------------------------------------------------------
# Argument parser
# ---------------------------------------------------------------------------

def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Convert ONNX model to INT8-quantized RKNN for RV1106.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument(
        "--onnx",
        required=True,
        type=Path,
        help="Path to the input ONNX model (e.g. yolov5n.onnx).",
    )
    parser.add_argument(
        "--dataset",
        required=True,
        type=Path,
        help="Path to a text file listing calibration images, one per line.",
    )
    parser.add_argument(
        "--output",
        required=True,
        type=Path,
        help="Output path for the generated .rknn file.",
    )
    parser.add_argument(
        "--target",
        default="rv1106",
        choices=["rv1103", "rv1106", "rk3566", "rk3568", "rk3588"],
        help="Target Rockchip platform (default: rv1106).",
    )
    parser.add_argument(
        "--mean",
        nargs=3,
        type=float,
        default=[0.0, 0.0, 0.0],
        metavar=("R", "G", "B"),
        help="Per-channel mean values for input normalisation (default: 0 0 0).",
    )
    parser.add_argument(
        "--std",
        nargs=3,
        type=float,
        default=[255.0, 255.0, 255.0],
        metavar=("R", "G", "B"),
        help="Per-channel std values for input normalisation (default: 255 255 255).",
    )
    parser.add_argument(
        "--verbose",
        action="store_true",
        help="Enable RKNN verbose logging.",
    )
    return parser

# ---------------------------------------------------------------------------
# Main conversion routine
# ---------------------------------------------------------------------------

def calibrate(args: argparse.Namespace) -> int:
    """
    Run the full ONNX → RKNN conversion pipeline.

    Returns 0 on success, 1 on failure.
    """
    # Validate inputs before touching the RKNN API.
    if not args.onnx.is_file():
        log.error("ONNX file not found: %s", args.onnx)
        return 1
    if not args.dataset.is_file():
        log.error("Dataset file not found: %s", args.dataset)
        return 1

    # Ensure output directory exists.
    args.output.parent.mkdir(parents=True, exist_ok=True)

    # ------------------------------------------------------------------
    # Late import — rknn-toolkit2 may not be installed in the host
    # Python environment (it lives inside the Docker container).
    # ------------------------------------------------------------------
    try:
        from rknn.api import RKNN  # type: ignore[import-untyped]
    except ImportError:
        log.error(
            "rknn-toolkit2 is not installed.\n"
            "  Run inside the Docker container:  "
            "docker run --rm -v $(pwd):/workspace gema-calibrate"
        )
        return 1

    rknn = RKNN(verbose=args.verbose)
    try:
        # ---- Step 1: Configure the target platform & normalisation ------
        log.info("Configuring RKNN — target: %s", args.target)
        ret = rknn.config(
            mean_values=[list(args.mean)],
            std_values=[list(args.std)],
            target_platform=args.target,
        )
        if ret != 0:
            log.error("rknn.config() failed with code %d", ret)
            return 1
        log.info("  mean_values : %s", args.mean)
        log.info("  std_values  : %s", args.std)

        # ---- Step 2: Load the ONNX model --------------------------------
        log.info("Loading ONNX model: %s", args.onnx)
        ret = rknn.load_onnx(model=str(args.onnx))
        if ret != 0:
            log.error("rknn.load_onnx() failed with code %d", ret)
            return 1

        # ---- Step 3: Build (quantize) ───────────────────────────────────
        log.info("Building RKNN model — do_quantization=True")
        ret = rknn.build(do_quantization=True, dataset=str(args.dataset))
        if ret != 0:
            log.error("rknn.build() failed with code %d", ret)
            return 1

        # ---- Step 4: Export the .rknn binary -----------------------------
        log.info("Exporting RKNN model: %s", args.output)
        ret = rknn.export_rknn(str(args.output))
        if ret != 0:
            log.error("rknn.export_rknn() failed with code %d", ret)
            return 1

        log.info("✅ Calibration complete — %s", args.output)
        return 0

    finally:
        # ---- Always release the RKNN context (even on failure) ----------
        log.info("Releasing RKNN context...")
        rknn.release()

# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main() -> None:
    parser = build_parser()
    args = parser.parse_args()
    sys.exit(calibrate(args))


if __name__ == "__main__":
    main()
