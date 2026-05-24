# RKNN Quantization — Horno de Calibración

Convierte modelos ONNX (exportados desde PyTorch) a binarios `.rknn`
cuantizados a INT8 para la NPU del Rockchip RV1106.

## Requisitos

- **Docker** instalado y funcionando

## Preparación del Dataset

1. Las fotos de calibración deben estar en una carpeta (ej. `./dataset/`).
2. Crear un archivo `dataset.txt` con las rutas, una por línea:

```text
dataset/frame_20260524_194700_batch1_cap0.jpg
dataset/frame_20260524_194700_batch1_cap1.jpg
...
```

> ⚠️ Las fotos deben ser del **mismo lugar, misma cámara, misma
> iluminación** donde se va a desplegar el modelo. Fotos genéricas
> degradan la precisión de la cuantización.

## Instalación de rknn-toolkit2

El wheel de rknn-toolkit2 se descarga del SDK oficial de Rockchip.
Si la descarga automática falla:

1. Descargar el wheel manualmente desde el SDK de Rockchip (Zbox,
   fetch code: `rknn`) para Python 3.8 / Ubuntu 20.04.
2. Colocarlo en `packages/rknn_toolkit2-*.whl`.
3. Construir la imagen:

```bash
docker build -f Dockerfile.calibrate \
    --build-arg RKNN_WHEEL_URL=file:///tmp/packages/rknn_toolkit2-*.whl \
    -t gema-calibrate .
```

## Uso

### 1. Construir la imagen Docker

```bash
cd edge_ops/gema_vision/scripts/quantization
docker build -f Dockerfile.calibrate -t gema-calibrate .
```

### 2. Ejecutar la calibración

```bash
docker run --rm                               \
    -v /ruta/a/tu/modelo:/workspace           \
    gema-calibrate                            \
    --onnx    model/yolov5n.onnx              \
    --dataset dataset/dataset.txt             \
    --output  model/yolov5n.rknn
```

### 3. El resultado

```
model/yolov5n.rknn    ← binario listo para deployar en el RV1106
```

## Argumentos

| Argumento     | Default       | Descripción                             |
|---------------|---------------|-----------------------------------------|
| `--onnx`      | *(requerido)* | Ruta al modelo ONNX de entrada          |
| `--dataset`   | *(requerido)* | TXT con lista de imágenes de calibración|
| `--output`    | *(requerido)* | Ruta del .rknn de salida                |
| `--target`    | `rv1106`      | Plataforma destino (`rv1103`, `rk3588`) |
| `--mean`      | `0 0 0`       | Media por canal para normalización      |
| `--std`       | `255 255 255` | Desvío por canal para normalización     |
| `--verbose`   | *(flag)*      | Log detallado de RKNN                   |

## Arquitectura

```text
PyTorch (.pt) ──▶ ONNX (.onnx) ──▶ calibrate.py ──▶ RKNN (.rknn)
                                       │
                              do_quantization=True
                              dataset.txt (200 fotos)
```

El script **no se ejecuta en el RV1106** — corre en tu PC o CI.
El `.rknn` resultante se copia al RV1106 para usarlo con `RknnContext`.
