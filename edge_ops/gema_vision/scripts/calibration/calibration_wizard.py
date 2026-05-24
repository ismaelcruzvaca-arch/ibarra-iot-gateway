#!/usr/bin/env python3
"""
calibration_wizard.py  —  GEMA Vision Camera Installation Tool
══════════════════════════════════════════════════════════════════════════════

Asistente de instalación para el técnico de Novamex en el piso de producción.

¿Qué hace?
──────────
1. Toma una foto del checkerboard pegado en la banda transportadora.
2. Detecta automáticamente las esquinas del patrón (cv2.findChessboardCorners).
3. Calcula la matriz de homografía H (píxeles → milímetros) para Z=0.
4. Genera el archivo config.json listo para copiar a la cámara.

Uso
───
    python calibration_wizard.py --image foto_checkerboard.jpg
    python calibration_wizard.py --image foto.jpg --square_size 25 --rows 9 --cols 6

Salida
──────
    Crea config.json en el directorio actual.

Flujo de instalación:
─────────────────────
    1. Pegar el checkerboard impreso sobre la banda transportadora,
       DEBAJO de la cámara.
    2. Tomar una foto (usar DataCollectorEngine en modo CALIBRATION,
       o cualquier cámara).
    3. Transferir la foto a la laptop del técnico.
    4. Ejecutar este script.
    5. Copiar el config.json generado a /userdata/vision/config.json
       en la cámara (vía SCP).
    6. Reiniciar el servicio gema-vision en la cámara.

Requisitos:
───────────
    pip install opencv-python numpy
"""

from __future__ import annotations

import argparse
import json
import os
import sys
from pathlib import Path

import cv2
import numpy as np


# ---------------------------------------------------------------------------
# Argument parser
# ---------------------------------------------------------------------------

def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="GEMA Vision — Asistente de calibración espacial.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument(
        "--image", "-i",
        required=True,
        type=Path,
        help="Ruta a la foto del checkerboard sobre la banda.",
    )
    parser.add_argument(
        "--square_size",
        type=float,
        default=20.0,
        help="Tamaño de cada cuadrado del checkerboard en mm (default: 20).",
    )
    parser.add_argument(
        "--rows",
        type=int,
        default=9,
        help="Filas de esquinas internas del checkerboard (default: 9).",
    )
    parser.add_argument(
        "--cols",
        type=int,
        default=6,
        help="Columnas de esquinas internas del checkerboard (default: 6).",
    )
    parser.add_argument(
        "--output",
        "-o",
        type=Path,
        default=Path("config.json"),
        help="Archivo de salida (default: config.json).",
    )
    return parser


# ---------------------------------------------------------------------------
# Core calibration
# ---------------------------------------------------------------------------

def calibrate(
    image_path: Path,
    square_size_mm: float,
    pattern_size: tuple[int, int],
) -> np.ndarray | None:
    """
    Detecta el checkerboard en la imagen y calcula la homografía H.

    Args:
        image_path: Ruta a la imagen.
        square_size_mm: Tamaño de cada cuadrado en mm.
        pattern_size: (rows, cols) de esquinas internas.

    Returns:
        Matriz H 3x3 como ndarray, o None si falla la detección.
    """
    img = cv2.imread(str(image_path))
    if img is None:
        print(f"  ❌ No se pudo leer la imagen: {image_path}")
        return None

    gray = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)
    h, w = gray.shape
    print(f"  📷 Imagen: {image_path.name} ({w}x{h})")

    # --- 1. Detectar esquinas del checkerboard -----------------------------
    ret, corners = cv2.findChessboardCorners(gray, pattern_size, None)
    if not ret:
        print(
            f"  ❌ No se detectó el checkerboard {pattern_size}.\n"
            f"     Verificá que el patrón sea visible y esté completo."
        )
        return None

    # Refinar a sub-píxel (precisión: fracción de pixel).
    criteria = (cv2.TERM_CRITERIA_EPS + cv2.TERM_CRITERIA_MAX_ITER, 30, 0.001)
    corners_refined = cv2.cornerSubPix(gray, corners, (11, 11), (-1, -1), criteria)

    print(f"  ✅ Checkerboard detectado: {len(corners_refined)} esquinas")

    # --- 2. Generar coordenadas físicas (mm) en Z=0 ------------------------
    objp = np.zeros((pattern_size[0] * pattern_size[1], 2), np.float32)
    objp[:, :2] = np.mgrid[0:pattern_size[0], 0:pattern_size[1]].T.reshape(-1, 2)
    objp *= square_size_mm  # ahora está en mm

    # --- 3. Calcular homografía --------------------------------------------
    H, mask = cv2.findHomography(corners_refined, objp, cv2.RANSAC, 5.0)
    if H is None:
        print("  ❌ cv2.findHomography() falló — no se pudo calcular H.")
        return None

    inliers = int(np.sum(mask))
    total = len(mask)
    print(f"  ✅ Homografía calculada: {inliers}/{total} inliers")
    print(f"  📐 H =\n{H}")

    return H


# ---------------------------------------------------------------------------
# Generate config.json
# ---------------------------------------------------------------------------

def generate_config(H: np.ndarray) -> dict:
    """Convierte la matriz H en un dict JSON con los 9 floats."""
    h_list = []
    for r in range(3):
        for c in range(3):
            h_list.append(round(float(H[r, c]), 10))

    return {
        "_comment": "GEMA Vision — Configuración de calibración espacial",
        "_description": "Generado por calibration_wizard.py. No modificar manualmente.",
        "homography": h_list,
        "product_height_mm": 0.0,
        "camera_height_mm": 500.0,
    }


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main() -> None:
    parser = build_parser()
    args = parser.parse_args()

    if not args.image.is_file():
        print(f"❌ Archivo no encontrado: {args.image}")
        sys.exit(1)

    pattern_size = (args.rows, args.cols)

    print(f"\n🔧 GEMA Vision — Asistente de Calibración\n")
    print(f"  📏 Checkerboard: {pattern_size[0]}x{pattern_size[1]}")
    print(f"  📐 Square size:  {args.square_size} mm")

    H = calibrate(args.image, args.square_size, pattern_size)
    if H is None:
        sys.exit(1)

    config = generate_config(H)

    with open(args.output, "w") as f:
        json.dump(config, f, indent=4)
        f.write("\n")

    print(f"\n  💾 Config guardado: {args.output}")
    print(f"\n  ▶ Próximo paso:")
    print(f"     scp {args.output} root@camara:/userdata/vision/config.json")
    print(f"     ssh root@camara 'systemctl restart gema-vision'")
    print(f"\n✅ Calibración completada.\n")


if __name__ == "__main__":
    main()
