#!/usr/bin/env python3
"""
dataset_gen.py — Synthetic OCR Dataset Generator for LPRNet Training.

Generates 24×94 BGR PNG images of industrial expiry date / lot code text
with 10 augmentation techniques targeting RV1106 LPRNet (18 character classes).

Usage:
    python dataset_gen.py --output ./dataset --count 20000 --seed 42
"""

import argparse
import io
import os
import random
import sys
import urllib.request
import zipfile
from pathlib import Path
from typing import List, Optional, Tuple

import cv2
import numpy as np
from PIL import Image, ImageDraw, ImageFont
from tqdm import tqdm

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

LPRNET_HEIGHT = 24
LPRNET_WIDTH = 94

# 19 active characters (indices 0..18) + blank at index 19 (not used in labels)
# IMPORTANT: blank is AFTER 'C' so using BLANK_IDX=19 for CTC loss
CHAR_SET = "0123456789/:LOTEVNC"

# High-contrast (top, bottom) background color pairs in BGR
# Each pair ensures text is readable on the color boundary
BG_COLOR_PAIRS: List[Tuple[Tuple[int, int, int], Tuple[int, int, int]]] = [
    ((60, 60, 60), (255, 255, 0)),            # dark gray / yellow
    ((255, 255, 255), (0, 0, 180)),           # white / blue
    ((0, 180, 0), (200, 50, 50)),             # green / red
    ((200, 200, 200), (50, 50, 50)),          # light gray / dark gray
    ((255, 255, 200), (0, 100, 0)),           # off-white / dark green
    ((80, 80, 80), (255, 200, 0)),            # dark / gold
    ((255, 200, 200), (100, 0, 0)),           # light pink / dark red
    ((200, 255, 200), (0, 80, 0)),            # light green / dark green
    ((180, 180, 255), (80, 80, 0)),           # light blue / olive
    ((50, 50, 100), (255, 220, 180)),         # navy / peach
    ((220, 220, 220), (0, 120, 120)),         # silver / teal
    ((100, 50, 50), (255, 255, 150)),         # maroon / pale yellow
]

# Line 1 templates (expiry date / VENCE).
# Format strings use date fields; spaces are converted to ':' in labels.
LINE1_TEMPLATES = [
    "VENCE {d:02d}/{m:02d}/{y:02d}",
    "V {d:02d}/{m:02d}/{y:02d}",
    "VENCE {d:02d}:{m:02d}:{y:02d}",
]

# Line 2 templates (lot / batch code).
# {n} expands to a random digit string, {d}/{m}/{y} reuse the date.
LINE2_TEMPLATES = [
    "LOTE {n}",
    "LOTE {n:04d}",
    "LOTE {d:02d}{m:02d}{y:02d}",
    "LOTE {d:02d}/{m:02d}/{y:02d}",
]

# URL sources for dot-matrix fonts (auto-download).
# The code handles zip archives and raw .ttf/.otf files.
FONT_SOURCES = {
    "PrintDOT": "https://raw.githubusercontent.com/kenmchugh/printdot-font/master/PrintDOT.ttf",
    "DotMatrix": "https://dl.dafont.com/dl/?f=dot_matrix",
}

# System fallback fonts (in priority order)
SYSTEM_FONT_PATHS = [
    # Windows
    "C:/Windows/Fonts/COURBD.TTF",
    "C:/Windows/Fonts/COUR.TTF",
    "C:/Windows/Fonts/ARIALBD.TTF",
    "C:/Windows/Fonts/consolab.ttf",
    "C:/Windows/Fonts/consola.ttf",
    # Linux
    "/usr/share/fonts/truetype/freefont/FreeMonoBold.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf",
    "/usr/share/fonts/TTF/DejaVuSansMono-Bold.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationMono-Bold.ttf",
    # macOS
    "/System/Library/Fonts/Courier.ttc",
    "/Library/Fonts/Courier New.ttf",
]


# ---------------------------------------------------------------------------
# Font management
# ---------------------------------------------------------------------------

def _discover_fonts(cache_dir: Path) -> List[str]:
    """Return a deduplicated list of available TrueType font paths."""
    seen = set()
    fonts: List[str] = []

    # 1. Cached downloaded fonts
    if cache_dir.is_dir():
        for ext in ("*.ttf", "*.otf", "*.ttc"):
            for fp in cache_dir.glob(ext):
                resolved = str(fp.resolve())
                if resolved not in seen:
                    seen.add(resolved)
                    fonts.append(resolved)

    # 2. System fallbacks
    for fp in SYSTEM_FONT_PATHS:
        p = Path(fp)
        if p.is_file():
            resolved = str(p.resolve())
            if resolved not in seen:
                seen.add(resolved)
                fonts.append(resolved)

    return fonts


def _download_fonts(cache_dir: Path) -> None:
    """Download dot-matrix fonts to the local cache directory.

    Skips fonts that already exist. Handles .zip and raw .ttf payloads.
    Failures are silent — the pipeline falls back to system fonts.
    """
    cache_dir.mkdir(parents=True, exist_ok=True)

    for name, url in FONT_SOURCES.items():
        target = cache_dir / f"{name}.ttf"
        if target.is_file():
            continue

        sys.stdout.write(f"[fonts] Downloading {name} ... ")
        sys.stdout.flush()
        try:
            req = urllib.request.Request(
                url, headers={"User-Agent": "Mozilla/5.0 (compatible; dataset_gen.py)"}
            )
            with urllib.request.urlopen(req, timeout=30) as resp:
                data = resp.read()

            # Handle zip archives
            is_zip = (
                url.endswith(".zip")
                or data[:2] == b"PK"
                or "application/zip" in resp.headers.get("Content-Type", "")
            )
            if is_zip:
                with zipfile.ZipFile(io.BytesIO(data)) as zf:
                    ttf_files = sorted(
                        n for n in zf.namelist()
                        if n.lower().endswith(".ttf") and "__MACOSX" not in n
                    )
                    if not ttf_files:
                        otf_files = sorted(
                            n for n in zf.namelist()
                            if n.lower().endswith(".otf") and "__MACOSX" not in n
                        )
                        ttf_files = otf_files
                    if ttf_files:
                        with zf.open(ttf_files[0]) as member:
                            with open(target, "wb") as f:
                                f.write(member.read())
                        sys.stdout.write(f"OK ({ttf_files[0]})\n")
                    else:
                        sys.stdout.write("SKIP (no font in zip)\n")
            else:
                with open(target, "wb") as f:
                    f.write(data)
                sys.stdout.write("OK\n")
        except Exception as exc:
            sys.stdout.write(f"FAIL ({exc})\n")
        sys.stdout.flush()


# ---------------------------------------------------------------------------
# Random text generator
# ---------------------------------------------------------------------------

def _rand_date() -> Tuple[int, int, int]:
    """Generate a realistic date for expiry codes (year 24-29)."""
    day = random.randint(1, 28)
    month = random.randint(1, 12)
    year = random.randint(24, 29)
    return day, month, year


def _rand_digits(k: int) -> str:
    """Return a random digit string of length *k*."""
    return "".join(str(random.randint(0, 9)) for _ in range(k))


def generate_text_pair() -> Tuple[str, str]:
    """Generate (visual_text_with_spaces, label_without_spaces).

    The visual text has spaces for rendering on the canvas.
    The label replaces all spaces + newlines with ':' so the
    output only contains characters from CHAR_SET.
    """
    day, month, year = _rand_date()

    # Pick templates
    line1_tmpl = random.choice(LINE1_TEMPLATES)
    line2_tmpl = random.choice(LINE2_TEMPLATES)

    # Fill line 1
    line1_visual = line1_tmpl.format(d=day, m=month, y=year)

    # Fill line 2 — handle {n} (random lot digits, int for :04d) and date reuse
    lot_int = random.randint(0, 9999)
    line2_visual = line2_tmpl.format(
        d=day,
        m=month,
        y=year,
        n=lot_int,  # int works for both {n} and {n:04d}
    )

    # Assemble visual (two lines, space-separated groups)
    visual_text = f"{line1_visual}\n{line2_visual}"

    # Build label: newline → ':', space → ':', then collapse runs
    label = visual_text.replace("\n", ":").replace(" ", ":")
    while "::" in label:
        label = label.replace("::", ":")
    # Strip leading/trailing colons
    label = label.strip(":")

    return visual_text, label


# ---------------------------------------------------------------------------
# Augmentation primitives
# ---------------------------------------------------------------------------

def _make_hybrid_bg(
    width: int,
    height: int,
    color_top: Tuple[int, int, int],
    color_bot: Tuple[int, int, int],
) -> np.ndarray:
    """Return a (height, width, 3) BGR array split horizontally at midline."""
    img = np.empty((height, width, 3), dtype=np.uint8)
    mid = height // 2
    img[:mid] = color_top
    img[mid:] = color_bot
    return img


def _render_multiline_text(
    text: str,
    font_paths: List[str],
    canvas_size: Tuple[int, int],
) -> Tuple[np.ndarray, Tuple[int, int, int, int]]:
    """Render *text* (may contain '\\n') onto a transparent RGBA PIL canvas.

    Returns (RGB_array, bounding_box_as_(x1, y1, x2, y2)).
    Text is white on transparent so the caller can composite with alpha.
    """
    cw, ch = canvas_size

    # Pick font size proportional to canvas height
    font_size = max(14, ch // 4)

    font = None
    for fp in font_paths:
        try:
            font = ImageFont.truetype(fp, font_size)
            break
        except (OSError, IOError):
            continue
    if font is None:
        font = ImageFont.load_default()

    # Create transparent layer
    pil = Image.new("RGBA", (cw, ch), (0, 0, 0, 0))
    draw = ImageDraw.Draw(pil)

    lines = text.split("\n")
    # Measure each line
    metrics = []
    total_h = 0
    for line in lines:
        bbox = draw.textbbox((0, 0), line, font=font)
        lw = bbox[2] - bbox[0]
        lh = bbox[3] - bbox[1]
        metrics.append((lw, lh, bbox))
        total_h += lh

    # Vertically center block
    y_cursor = max(0, (ch - total_h) // 2)

    # Union bbox across all lines
    x1, y1, x2, y2 = cw, ch, 0, 0
    for line, (lw, lh, bbox) in zip(lines, metrics):
        x = max(0, (cw - lw) // 2)
        y = y_cursor
        draw.text((x, y), line, font=font, fill=(255, 255, 255, 255))

        x1 = min(x1, x)
        y1 = min(y1, y)
        x2 = max(x2, x + lw)
        y2 = max(y2, y + lh)
        y_cursor += lh + 2

    arr = np.array(pil, dtype=np.uint8)
    text_rgb = arr[:, :, :3]  # RGB
    return text_rgb, (x1, y1, x2, y2)


def _composite_text(
    bg: np.ndarray,
    text_rgb: np.ndarray,
    text_bbox: Tuple[int, int, int, int],
) -> np.ndarray:
    """Blit white/black text onto *bg* choosing max contrast per region.

    Uses the text alpha mask from *text_rgb* (white pixels == text) and
    inverts the local background brightness for the text colour.
    """
    result = bg.copy()
    h, w = text_rgb.shape[:2]
    gray = text_rgb.mean(axis=2)  # brightness, (h, w)
    mask = gray > 20  # text pixels

    if not mask.any():
        return result

    x1, y1, x2, y2 = text_bbox
    x1 = max(0, x1)
    y1 = max(0, y1)
    x2 = min(w, x2)
    y2 = min(h, y2)
    if x2 <= x1 or y2 <= y1:
        return result

    # Sample the background under the text bounding box
    bg_region = result[y1:y2, x1:x2]
    avg_brightness = bg_region.mean()

    text_color = (0, 0, 0) if avg_brightness > 127 else (255, 255, 255)
    # Apply only where mask is active within the crop region
    region_mask = mask[y1:y2, x1:x2]
    result[y1:y2, x1:x2][region_mask] = text_color

    return result


def _cylindrical_distortion(
    img: np.ndarray,
    radius: Optional[float] = None,
) -> np.ndarray:
    """Simulate curved bottle surface via cylindrical remapping.

    *radius* 200-500 produces subtle to noticeable curvature on a ~400 px canvas.
    """
    h, w = img.shape[:2]
    if radius is None:
        radius = random.uniform(200.0, 500.0)

    cx = w / 2.0
    cy = h / 2.0

    yy, xx = np.meshgrid(np.arange(h, dtype=np.float32),
                          np.arange(w, dtype=np.float32),
                          indexing="ij")

    dx = (xx - cx) / radius
    src_x = cx + radius * np.sin(dx)
    src_y = cy + (yy - cy) * np.cos(dx)

    return cv2.remap(img, src_x.astype(np.float32), src_y.astype(np.float32),
                     cv2.INTER_LINEAR, borderMode=cv2.BORDER_REPLICATE)


def _rotate_extreme(
    img: np.ndarray,
    angle: float,
    fill_color: Tuple[int, int, int],
) -> np.ndarray:
    """Rotate image by *angle* degrees, expanding canvas and filling with *fill_color*."""
    h, w = img.shape[:2]
    center = (w / 2.0, h / 2.0)
    mat = cv2.getRotationMatrix2D(center, angle, 1.0)

    cos_a = abs(mat[0, 0])
    sin_a = abs(mat[0, 1])
    new_w = int(h * sin_a + w * cos_a)
    new_h = int(h * cos_a + w * sin_a)

    mat[0, 2] += new_w / 2.0 - center[0]
    mat[1, 2] += new_h / 2.0 - center[1]

    return cv2.warpAffine(img, mat, (new_w, new_h),
                          borderValue=fill_color)


def _add_dot_noise(img: np.ndarray, density: float) -> np.ndarray:
    """Salt-and-pepper noise with given *density* (0.0-1.0)."""
    out = img.copy()
    h, w = img.shape[:2]
    mask = np.random.rand(h, w)
    out[mask < density * 0.5] = 255     # salt
    out[mask > 1.0 - density * 0.5] = 0  # pepper
    return out


def _add_motion_blur(img: np.ndarray, ksize: int) -> np.ndarray:
    """Horizontal motion blur with a (ksize, ksize) kernel."""
    kernel = np.zeros((ksize, ksize), dtype=np.float32)
    kernel[ksize // 2, :] = 1.0 / ksize
    return cv2.filter2D(img, -1, kernel)


def _add_specular_glare(
    img: np.ndarray,
    width_frac: float = 0.2,
) -> np.ndarray:
    """Overlay a semi-transparent white patch simulating plastic glare."""
    h, w = img.shape[:2]
    out = img.copy()

    gw = max(5, int(w * width_frac * random.uniform(0.5, 1.5)))
    gh = max(3, int(h * 0.3 * random.uniform(0.5, 1.5)))
    gw = min(gw, w)
    gh = min(gh, h)

    x = random.randint(0, w - gw) if gw < w else 0
    y = random.randint(0, h - gh) if gh < h else 0

    alpha = random.uniform(0.1, 0.4)
    white_patch = np.full((gh, gw, 3), 255, dtype=np.uint8)

    roi = out[y:y + gh, x:x + gw]
    blended = cv2.addWeighted(roi, 1.0 - alpha, white_patch, alpha, 0)
    out[y:y + gh, x:x + gw] = blended

    return out


def _add_gaussian_blur(img: np.ndarray, sigma: float) -> np.ndarray:
    """Gaussian blur. Sigma is clamped to [0.2, 3.0]."""
    sigma = max(0.2, min(sigma, 3.0))
    ksize = int(2 * round(3.0 * sigma) + 1)
    ksize = max(3, ksize)
    if ksize % 2 == 0:
        ksize += 1
    return cv2.GaussianBlur(img, (ksize, ksize), sigma)


def _apply_scale_jitter(
    img: np.ndarray,
    target_h: int,
    target_w: int,
    scale: float,
) -> np.ndarray:
    """Resize then centre-crop/pad to (target_h, target_w) by *scale* factor."""
    if abs(scale - 1.0) < 0.005:
        # No-op — just ensure exact size
        return cv2.resize(img, (target_w, target_h), interpolation=cv2.INTER_LINEAR)

    new_h = max(target_h, int(target_h * scale))
    new_w = max(target_w, int(target_w * scale))
    resized = cv2.resize(img, (new_w, new_h), interpolation=cv2.INTER_LINEAR)

    if scale >= 1.0:
        # Centre crop
        sy = (new_h - target_h) // 2
        sx = (new_w - target_w) // 2
        return resized[sy:sy + target_h, sx:sx + target_w]
    else:
        # Centre pad with black
        pad_y = (target_h - new_h) // 2
        pad_x = (target_w - new_w) // 2
        canvas = np.zeros((target_h, target_w, 3), dtype=np.uint8)
        canvas[pad_y:pad_y + new_h, pad_x:pad_x + new_w] = resized
        return canvas


def _adjust_contrast(img: np.ndarray, alpha: float) -> np.ndarray:
    """Linear contrast adjustment: ``out = in * alpha``."""
    return cv2.convertScaleAbs(img, alpha=alpha, beta=0)


# ---------------------------------------------------------------------------
# Full sample generation pipeline
# ---------------------------------------------------------------------------

def generate_sample(font_paths: List[str]) -> Tuple[np.ndarray, str]:
    """Generate one (image, label) pair.

    Pipeline
    --------
    1.  Two-tone background   (REQ-002)
    2.  Render two-line text  (REQ-001)
    3.  Composite text onto bg
    4.  Cylindrical distortion
    5.  Extreme rotation      (REQ-003)
    6.  Centre crop + resize → 24×94
    7.  Dot noise             (REQ-004-4)
    8.  Motion blur           (REQ-004-5)
    9.  Specular glare        (REQ-004-6)
    10. Gaussian blur         (REQ-004-7)
    11. Scale / zoom jitter   (REQ-004-8)
    12. Contrast variation    (REQ-004-9)
    """
    # ---- 0. Text ----------------------------------------------------------
    visual_text, label = generate_text_pair()

    # ---- 1. Background ----------------------------------------------------
    color_top, color_bot = random.choice(BG_COLOR_PAIRS)
    canvas_w, canvas_h = 500, 160  # generous — room for ±70° rotation
    img = _make_hybrid_bg(canvas_w, canvas_h, color_top, color_bot)

    # ---- 2-3. Render + composite text ------------------------------------
    text_rgb, bbox = _render_multiline_text(visual_text, font_paths,
                                            (canvas_w, canvas_h))
    img = _composite_text(img, text_rgb, bbox)

    # ---- 4. Cylindrical distortion (≈70% chance) -------------------------
    if random.random() < 0.70:
        img = _cylindrical_distortion(img, radius=random.uniform(200, 500))

    # ---- 5. Extreme rotation ±70° ----------------------------------------
    angle = random.uniform(-70.0, 70.0)
    fill = random.choice([color_top, color_bot])
    img = _rotate_extreme(img, angle, fill)

    # ---- 6. Centre-crop text region → 24×94 ------------------------------
    h, w = img.shape[:2]
    # Crop a generous rectangle around the centre
    crop_ratio = random.uniform(0.35, 0.65)
    crop_h = max(LPRNET_HEIGHT, int(h * crop_ratio))
    crop_w = max(LPRNET_WIDTH, int(w * crop_ratio))

    # Clamp to image bounds
    crop_h = min(crop_h, h)
    crop_w = min(crop_w, w)

    y_start = max(0, (h - crop_h) // 2 + random.randint(-8, 8))
    x_start = max(0, (w - crop_w) // 2 + random.randint(-8, 8))
    y_start = min(y_start, h - crop_h)
    x_start = min(x_start, w - crop_w)

    img = img[y_start:y_start + crop_h, x_start:x_start + crop_w]
    img = cv2.resize(img, (LPRNET_WIDTH, LPRNET_HEIGHT),
                     interpolation=cv2.INTER_LINEAR)

    # ---- 7-12. Pixel-level augmentations ---------------------------------
    if random.random() < 0.80:
        img = _add_dot_noise(img, density=random.uniform(0.005, 0.02))

    if random.random() < 0.60:
        img = _add_motion_blur(img, ksize=random.choice([3, 5]))

    if random.random() < 0.40:
        img = _add_specular_glare(img, width_frac=random.uniform(0.10, 0.30))

    if random.random() < 0.50:
        img = _add_gaussian_blur(img, sigma=random.uniform(0.5, 1.5))

    if random.random() < 0.50:
        scale = random.uniform(0.90, 1.10)
        img = _apply_scale_jitter(img, LPRNET_HEIGHT, LPRNET_WIDTH, scale)

    if random.random() < 0.70:
        img = _adjust_contrast(img, alpha=random.uniform(0.7, 1.3))

    return img, label


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def parse_args(argv: Optional[List[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate synthetic OCR dataset for LPRNet training (18-class industrial chars).",
    )
    parser.add_argument(
        "--output", "-o",
        default="./dataset",
        help="Output directory (default: ./dataset)",
    )
    parser.add_argument(
        "--count", "-n",
        type=int,
        default=20000,
        help="Total number of samples (default: 20000)",
    )
    parser.add_argument(
        "--seed", "-s",
        type=int,
        default=42,
        help="Random seed (default: 42)",
    )
    parser.add_argument(
        "--train-ratio",
        type=float,
        default=0.8,
        help="Fraction for training split (default: 0.8)",
    )
    return parser.parse_args(argv)


def _setup_dirs(output: str, splits: List[Tuple[str, int]]) -> None:
    """Create output directory tree."""
    for name, _ in splits:
        (Path(output) / name / "images").mkdir(parents=True, exist_ok=True)
        (Path(output) / name / "labels").mkdir(parents=True, exist_ok=True)


def _write_label(lbl_dir: Path, stem: str, label: str) -> None:
    """Write a one-line label file."""
    (lbl_dir / f"{stem}.txt").write_text(label + "\n", encoding="ascii")


def main(argv: Optional[List[str]] = None) -> None:
    args = parse_args(argv)

    random.seed(args.seed)
    np.random.seed(args.seed)

    out_dir = Path(args.output)
    train_count = int(args.count * args.train_ratio)
    test_count = args.count - train_count
    splits = [("train", train_count), ("test", test_count)]

    print(f"{'=' * 52}")
    print(f"  OCR LPRNet — Synthetic Dataset Generator")
    print(f"{'=' * 52}")
    print(f"  Output dir : {out_dir.resolve()}")
    print(f"  Total img  : {args.count:,}")
    print(f"    Train    : {train_count:,}")
    print(f"    Test     : {test_count:,}")
    print(f"  Seed       : {args.seed}")
    print(f"{'=' * 52}")

    # ---- Font bootstrap ---------------------------------------------------
    font_cache = Path(__file__).resolve().parent / ".fonts"
    _download_fonts(font_cache)
    font_paths = _discover_fonts(font_cache)

    if not font_paths:
        print("[!] WARNING: no TrueType fonts found. Falling back to PIL default.")
        print("    Labels will still be correct but rendered text may be tiny.")
    else:
        print(f"[fonts] {len(font_paths)} face(s) available:")
        for fp in font_paths[:5]:
            print(f"  • {fp}")

    # ---- Directory tree ---------------------------------------------------
    _setup_dirs(str(out_dir), splits)

    # ---- Generation -------------------------------------------------------
    total_generated = 0
    for split_name, split_count in splits:
        img_dir = out_dir / split_name / "images"
        lbl_dir = out_dir / split_name / "labels"

        pbar = tqdm(total=split_count, desc=f"  {split_name}", unit="img",
                    leave=True)
        for i in range(split_count):
            image, label = generate_sample(font_paths)

            stem = f"{split_name}_{i:06d}"
            cv2.imwrite(str(img_dir / f"{stem}.png"), image)
            _write_label(lbl_dir, stem, label)

            total_generated += 1
            pbar.update(1)
        pbar.close()

    # ---- Summary ----------------------------------------------------------
    print(f"\n{'=' * 52}")
    print(f"  DONE — {total_generated:,} samples written.")
    print(f"  • {out_dir}/train/images/  ({train_count:,} files)")
    print(f"  • {out_dir}/train/labels/  ({train_count:,} files)")
    print(f"  • {out_dir}/test/images/   ({test_count:,} files)")
    print(f"  • {out_dir}/test/labels/   ({test_count:,} files)")
    print(f"{'=' * 52}")

    # Show a few label examples
    ex_dir = out_dir / "train" / "labels"
    if ex_dir.is_dir():
        samples = sorted(ex_dir.glob("*.txt"))[:5]
        if samples:
            print("\n  Label samples:")
            for sp in samples:
                content = sp.read_text().strip()
                print(f"    {sp.name:>20s}  ->  {content}")
    print()


if __name__ == "__main__":
    main()
