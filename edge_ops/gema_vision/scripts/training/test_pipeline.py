#!/usr/bin/env python3
"""
test_pipeline.py — End-to-end validation of the OCR LPRNet pipeline.

Generates 100 test images using dataset_gen.py, then validates:

  1. All images are exactly 24×94 BGR (CV_8UC3).
  2. All labels contain only valid characters from the CHAR_SET.
  3. All label files exist and are non-empty.
  4. The generated dataset directory structure is correct.

Usage:
    python test_pipeline.py [--output ./test_dataset] [--seed 123]

Returns exit code 0 on success, 1 on failure.
"""

import argparse
import os
import sys
import tempfile
from pathlib import Path
from typing import List, Optional, Tuple

import cv2
import numpy as np

# Import the generator's CHAR_SET for label validation
from dataset_gen import CHAR_SET


# ---------------------------------------------------------------------------
# Validation helpers
# ---------------------------------------------------------------------------

def validate_image(path: Path) -> Tuple[bool, str]:
    """Check a single image file.

    Returns (pass, message).
    """
    img = cv2.imread(str(path), cv2.IMREAD_COLOR)
    if img is None:
        return False, f"Failed to read: {path.name}"

    if img.shape != (24, 94, 3):
        return False, (
            f"{path.name}: expected (24, 94, 3) BGR, "
            f"got {img.shape}"
        )

    if img.dtype != np.uint8:
        return False, f"{path.name}: dtype is {img.dtype}, expected uint8"

    return True, ""


def validate_label(path: Path) -> Tuple[bool, str]:
    """Check a single label file.

    Returns (pass, message).
    """
    if not path.is_file():
        return False, f"Label not found: {path.name}"

    text = path.read_text(encoding="ascii").strip()
    if not text:
        return False, f"Empty label: {path.name}"

    for ch in text:
        if ch not in CHAR_SET:
            return False, (
                f"{path.name}: invalid char {ch!r} "
                f"(not in CHAR_SET: {CHAR_SET!r})"
            )

    return True, ""


def validate_split(split_dir: Path, split_name: str) -> Tuple[int, int, int]:
    """Validate all images and labels in a split directory.

    Returns (total, passed_imgs, passed_labels).
    """
    img_dir = split_dir / "images"
    lbl_dir = split_dir / "labels"

    if not img_dir.is_dir():
        print(f"  FAIL: {split_name}/images/ directory not found")
        return 0, 0, 0
    if not lbl_dir.is_dir():
        print(f"  FAIL: {split_name}/labels/ directory not found")
        return 0, 0, 0

    images = sorted(img_dir.glob("*.png"))
    labels = sorted(lbl_dir.glob("*.txt"))

    print(f"\n  [{split_name}] {len(images)} images, {len(labels)} labels")

    passed_imgs = 0
    failed_imgs: List[str] = []
    passed_labels = 0
    failed_labels: List[str] = []

    # Validate images
    for img_path in images:
        ok, msg = validate_image(img_path)
        if ok:
            passed_imgs += 1
        else:
            failed_imgs.append(msg)

    # Validate labels
    for lbl_path in labels:
        ok, msg = validate_label(lbl_path)
        if ok:
            passed_labels += 1
        else:
            failed_labels.append(msg)

    # Check matching: every image should have a label and vice versa
    img_stems = {p.stem for p in images}
    lbl_stems = {p.stem for p in labels}
    images_without_labels = img_stems - lbl_stems
    labels_without_images = lbl_stems - img_stems

    if images_without_labels:
        print(f"    WARN: {len(images_without_labels)} image(s) without label")

    if labels_without_images:
        print(f"    WARN: {len(labels_without_images)} label(s) without image")

    # Print failures
    for msg in failed_imgs:
        print(f"    FAIL: {msg}")
    for msg in failed_labels:
        print(f"    FAIL: {msg}")

    print(f"    Images: {passed_imgs}/{len(images)} passed"
          + (f"  ({len(failed_imgs)} failed)" if failed_imgs else ""))
    print(f"    Labels: {passed_labels}/{len(labels)} passed"
          + (f"  ({len(failed_labels)} failed)" if failed_labels else ""))

    total = len(images)
    return total, passed_imgs, passed_labels


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def parse_args(argv: Optional[List[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Validate the OCR LPRNet pipeline with synthetic data.",
    )
    parser.add_argument(
        "--output", "-o",
        default=None,
        help="Output directory for generated test images "
             "(default: temp directory, auto-cleaned)",
    )
    parser.add_argument(
        "--seed", "-s",
        type=int,
        default=123,
        help="Random seed (default: 123)",
    )
    parser.add_argument(
        "--keep",
        action="store_true",
        help="Keep the output directory on success (useful for inspection)",
    )
    return parser.parse_args(argv)


def main(argv: Optional[List[str]] = None) -> int:
    args = parse_args(argv)

    print("=" * 60)
    print("  OCR LPRNet — Pipeline Validation")
    print("=" * 60)

    # ---- Resolve output directory -----------------------------------------
    if args.output:
        out_dir = Path(args.output)
        out_dir.mkdir(parents=True, exist_ok=True)
        own_temp = False
    else:
        out_dir = Path(tempfile.mkdtemp(prefix="ocr_lprnet_test_"))
        own_temp = True

    try:
        # ---- Step 1: Generate 100 synthetic images -------------------------
        print(f"\n[step 1] Generating 100 test images...")
        print(f"         Output : {out_dir.resolve()}")
        print(f"         Seed   : {args.seed}")

        # Import and run dataset_gen programmatically
        from dataset_gen import main as gen_main

        # dataset_gen's main() expects sys.argv — call it with our args
        gen_argv = [
            "dataset_gen.py",
            "--output", str(out_dir),
            "--count", "100",
            "--seed", str(args.seed),
        ]
        try:
            gen_main(gen_argv)
        except SystemExit as exc:
            if exc.code != 0:
                print(f"\n  FAIL: dataset_gen.py exited with code {exc.code}")
                return 1

        print(f"\n[step 1] Generation complete.")

        # ---- Step 2: Validate images and labels ----------------------------
        print(f"\n[step 2] Validating images and labels...")

        total_imgs = 0
        total_passed_imgs = 0
        total_passed_labels = 0

        for split_name in ("train", "test"):
            split_dir = out_dir / split_name
            total, p_imgs, p_lbls = validate_split(split_dir, split_name)
            total_imgs += total
            total_passed_imgs += p_imgs
            total_passed_labels += p_lbls

        # ---- Step 3: Summary -----------------------------------------------
        print(f"\n{'=' * 60}")
        print(f"  Results")
        print(f"{'=' * 60}")
        print(f"  Total images generated : {total_imgs}")
        print(f"  Images passed          : {total_passed_imgs}")
        print(f"  Labels passed          : {total_passed_labels}")
        images_failed = total_imgs - total_passed_imgs
        labels_failed = total_imgs - total_passed_labels
        print(f"  Images failed          : {images_failed}")
        print(f"  Labels failed          : {labels_failed}")

        if images_failed == 0 and labels_failed == 0 and total_imgs > 0:
            print(f"\n  ✅ ALL VALIDATIONS PASSED")
            result = 0
        else:
            print(f"\n  ❌ SOME VALIDATIONS FAILED")
            result = 1

        print(f"{'=' * 60}")
        return result

    finally:
        # Clean up temp directory unless --keep was passed
        if own_temp and not args.keep and out_dir.exists():
            import shutil
            shutil.rmtree(out_dir)


if __name__ == "__main__":
    sys.exit(main())
