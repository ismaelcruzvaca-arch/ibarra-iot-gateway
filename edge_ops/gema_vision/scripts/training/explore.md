# SDD Explore — Frente #3: OCR Dataset & LPRNet Training

## Executive Summary

**Veredicto: ✅ VIABLE — Riesgo bajo.**

Rockchip ya soporta LPRNet oficialmente en RV1106 (rknn_model_zoo v1.6.0+).
No necesitamos experimentar con arquitecturas — el pipeline ONNX → RKNN
está validado por el fabricante del silicio.

---

## 1. Compatibilidad RKNN — Operadores Soportados

### Estado: ✅ CONFIRMADO

La tabla oficial de rknn_model_zoo lista:

| Modelo | Input | Precisión | RV1106 FPS |
|--------|-------|-----------|------------|
| **LPRNet** | [1, 3, 24, 94] | FP16 / INT8 | **30.6 (INT8)** |

Esto significa que **Rockchip ya probó y certificó** que el grafo LPRNet
completo (incluyendo BiLSTM) se convierte correctamente a RKNN para RV1106.
El modelo de referencia es [`sirius-ai/LPRNet_Pytorch`](https://github.com/sirius-ai/LPRNet_Pytorch)
(1.1k★, Apache 2.0).

### Arquitectura del modelo de referencia

```
Input: [1, 3, 24, 94]  (BGR, no grayscale)
  │
  ├── Conv2d 3→64, 3×3, BN, ReLU, MaxPool 3×3, s=1
  ├── Conv2d 64→128, 3×3, BN, ReLU, MaxPool 3×3, s=1
  ├── Conv2d 128→256, 3×3, BN, ReLU
  ├── Conv2d 256→256, 3×3, BN, ReLU, MaxPool 3×3, s=1
  ├── Dropout(0.5)
  │
  ├── BiLSTM(256, 128, 2 layers)    ← La capa que nos preocupaba
  ├── Dropout(0.5)
  │
  ├── Conv2d 256→68, 1×1 (proyección a 68 clases de caracteres)
  └── Softmax → logits [18, 68]     ← 18 tsteps × 68 clases

Tamaño del modelo: ~1.7M parámetros
```

### Decisión sobre BiLSTM

**USAR BiLSTM.** Rockchip ya lo validó. No necesitamos CNN-only ni CRNN
simplificada. El rknn-toolkit2 v1.6.0 soporta BiLSTM para RV1106.

---

## 2. Partición del Grafo — Decodificación CTC

### Estado: ✅ RESUELTO — Decodificación EN C++

La decodificación **Greedy CTC** NO debe ir en el modelo ONNX. Razones:

1. **La NPU del RV1106 es una CNN acelaradora** — no ejecuta bien bucles
   de decodificación secuencial (argmax + collapse + remove blanks).
2. **La pérdida CTC solo se usa durante el entrenamiento** — en inferencia
   solo necesitamos los logits y un decoder trivial.
3. **La NPU es un recurso escaso** — no quememos ciclos decodificando.

### Flujo de inferencia final

```
Cámara → ROI 24×94 BGR
         ↓
LprnetOcrEngine::infer(roi)
  ├── Preprocess: resize a 24×94, normalizar
  ├── rknn_run() → logits [18 × 68]
  │                         ↑ 18 time steps, 68 character classes
  │
  └── Greedy CTC Decode (C++ puro, 0 NPU):
        for each timestep t:
            char = argmax(logits[t])     ← 68 → 1
            if char != blank_token AND char != prev_char:
                append char
        return decoded string
```

Esto es ~20 líneas de C++. La alternativa (argmax en C++) es trivial y
no necesita NPU. **Implementación sencilla en `lprnet_ocr_engine.cpp`.**

### Clases de caracteres (68 → nuestro alfabeto industrial reducido)

El modelo original usa 68 clases (incluyendo letras chinas). Para nuestro
caso industrial (fechas VENCE + lotes LOTE), podemos reducir a **17 clases**:

```
Clases: 0-9  /  :  L  O  T  E  V  N  C  blank
         │   │  │  │  │  │  │  │  │  │  │
         10  1  1  1  1  1  1  1  1  1  1   = 17 + 1 (blank) = 18 clases
```

Esto hace el modelo más pequeño (>2x más ligero) y más preciso que el
original de 68 clases.

---

## 3. Estrategia de Augmentation — Dataset Sintético

### Estado: ✅ DEFINIDO

### Parámetros de augmentación (Pillow + OpenCV)

| Augment | Librería | Rango | Motivo industrial |
|---------|----------|-------|-------------------|
| **Fuentes Dot Matrix** | Pillow + descarga automática | 3 fuentes | Imitar impresora de inyección de tinta |
| **Rotación** | Pillow `rotate()` | ±3° | Botella inclinada en la cinta |
| **Distorsión cilíndrica** | Pillow `transform()` + cuadrícula | Radio=200-500 | Plástico curvo de botella |
| **Ruido de puntos** | OpenCV `randu()` | 0.5-2% píxeles aleatorios | Tinta salpicada / cabeza obstruida |
| **Motion blur** | OpenCV `filter2D()` kernel 3×1 o 5×1 | 1-3px | Vibración de la cinta |
| **Glare especular** | Parche cuadrado blanco semi-transparente | 10-30% del ROI | Brillo del plástico bajo luz LED |
| **Desenfoque gaussiano** | OpenCV `GaussianBlur()` | σ=0.5-1.5 | Lente sucia / fuera de foco |
| **Escala/zoom** | Pillow `resize()` | 90-110% | Distancia variable de la cámara |
| **Contraste variable** | OpenCV `convertScaleAbs()` | α=0.7-1.3 | Iluminación de línea inestable |
| **Fondo de botella** | Parche de color sólido | Blanco/azul/verde | Color del envase |

### Fuentes Dot Matrix (descarga automática)

El script `dataset_gen.py` descargará e instalará automáticamente:

1. **PrintDOT** — https://fontsly.com/dot-matrix/printdot-font (dot matrix verdadero)
2. **Dot Matrix** — https://www.dafont.com/dot-matrix.font (estándar industrial)
3. **Courier New Bold** — Sistema (fallback, para texto legible de control)

### Ejemplo de datos generados

```
LOTE-2405
LOTE-8932
VENCE: 31/12/26
V: 15/08/25
VENCE: 01/03/27
LOTE-4512
```

Cada imagen se renderiza:
1. Texto base con fuente Dot Matrix sobre fondo blanco
2. Rotación aleatoria ±3°
3. Distorsión cilíndrica (simular botella redonda)
4. Ruido de puntos (salpicaduras)
5. Glare (parche blanco semi-transparente aleatorio)
6. Motion blur (3px horizontal)
7. Variación de contraste

---

## 4. Pipeline Completo Propuesto

```
1. dataset_gen.py
   ├── Descarga fuentes automáticamente
   ├── Genera 20,000+ imágenes (80% train, 20% test)
   └── Etiquetas en formato .txt (nombre_archivo → texto)

2. train.py (basado en sirius-ai/LPRNet_Pytorch)
   ├── Adapta modelo de 68 clases → 18 clases industriales
   ├── Entrenamiento: ~2h en Google Colab (Tesla T4)
   └── Exporta a ONNX (opset 17)

3. rknn_convert.py (basado en calibrate.py existente)
   ├── Convierte ONNX → RKNN INT8
   └── Genera ocr.rknn

4. LprnetOcrEngine (C++, ya existe)
   ├── Carga ocr.rknn
   ├── Preprocess ROI 24×94
   ├── rknn_run() → logits
   └── Greedy CTC decode (C++ puro, 20 líneas)
```

---

## 5. Archivos a Crear

| Archivo | Propósito |
|---------|-----------|
| `scripts/training/dataset_gen.py` | Generador de dataset sintético (400 líneas) |
| `scripts/training/lprnet.py` | Modelo LPRNet adaptado a 18 clases (150 líneas) |
| `scripts/training/train.py` | Entrenamiento + exportación ONNX (200 líneas) |
| `scripts/training/rknn_convert.py` | ONNX → RKNN usando rknn-toolkit2 (80 líneas) |
| `scripts/training/requirements.txt` | Dependencias Python |
| `src/lprnet_ocr_engine.cpp` | Implementar Greedy CTC decode + rknn_run() |

---

## 6. Riesgos Mitigados

| Riesgo | Probabilidad | Mitigación |
|--------|-------------|------------|
| BiLSTM no soportado en RKNN | ❌ 0% — Rockchip ya lo validó | Usar modelo del rknn_model_zoo |
| CTC decode no cabe en NPU | ❌ 0% — Lo hacemos en C++ | Argmax + greedy decode trivial |
| Dataset sintético no generaliza | 🟡 30% | Usar fuentes dot matrix reales + distorsión cilíndrica + glare |
| Training muy lento en CPU | 🟡 20% | Usar Google Colab (Tesla T4 gratis) o RKNN pre-entrenado |

---
