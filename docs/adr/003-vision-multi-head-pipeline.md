# ADR-003: Pipeline de Visión Multi-Etapa (Híbrido C++ / IA .rknn)

**Estado:** Aceptado
**Fecha:** 2026-05-21
**Contexto:** Inspección visual en línea de empaque — necesita OCR, colorimetría,
detección de presencia, y clasificación de defectos a velocidad de línea.

---

## Decisión

Se descarta el enfoque de **modelo de IA monolítico** (una sola red neuronal
que lo resuelve todo) a favor de una **arquitectura híbrida**:

- **C++ nativo** en el RV1106 para tareas determinísticas: colorimetría
  (cálculo L*a*b*), detección de bordes, conteo de píxeles, umbralizado.
- **Mini-modelos IA .rknn** sobre recortes (crops) de alta resolución para
  tareas de clasificación: OCR de caracteres en relieve, detección de
  defectos superficiales, validación de presencia de etiqueta.

## Rationale

1. **Rendimiento determinado por hardware:** El RV1106 tiene un NPU de 0.5 TOPS.
   Un modelo monolítico de alta resolución saturaría el NPU y no alcanzaría
   la velocidad de línea. Los mini-modelos sobre crops reducen el área de
   inferencia 10–50×.

2. **Precisión en colorimetría:** Las redes neuronales no son confiables para
   medir color absoluto (producen "alucinaciones" cromáticas). El cálculo
   L*a*b* en C++ con matriz de calibración es determinístico y trazable a
   patrones de calibración física.

3. **Mantenibilidad:** Un pipeline multi-etapa permite actualizar o reemplazar
   un mini-modelo sin afectar al resto del sistema. Un modelo monolítico
   requiere re-entrenamiento completo y validación E2E.

4. **Depurabilidad:** Cada etapa produce una salida intermedia que puede
   inspeccionarse visualmente (crops con anotaciones, mapas de calor). En
   un modelo monolítico, los errores son una caja negra.

## Arquitectura del Pipeline

```
Sensor RAW (IMX296 / AR0234CS)
       │
       ▼
  ┌─────────────────────┐
  │ Stage 1: Capture    │  C++ nativo (ISP + frame grab)
  │    & Debayer        │
  └────────┬────────────┘
           │
           ▼
  ┌─────────────────────┐
  │ Stage 2: Crop       │  C++ nativo — extrae ROI basado en
  │    Engine           │  coordenadas predefinidas por SKU
  └───┬──────────┬──────┘
      │          │
      ▼          ▼
  ┌────────┐ ┌────────┐
  │ Crop A │ │ Crop B │  Múltiples ROIs independientes
  │ (OCR)  │ │ (Color)│
  └───┬────┘ └───┬────┘
      │          │
      ▼          ▼
  ┌────────┐ ┌────────────┐
  │  IA    │ │  C++ Lab   │  Etapa 3: Inferencia paralela
  │ .rknn  │ │  Math      │  NPU + CPU simultáneos
  └───┬────┘ └──────┬─────┘
      │             │
      ▼             ▼
  ┌────────────────────────┐
  │ Stage 4: Fusion        │  C++ nativo — combina resultados,
  │    & Verdict           │  aplica lógica de negocio por SKU
  └──────────┬─────────────┘
             │
             ▼
         PASS / FAIL / REJECT
```

## Modelos .rknn Esperados

| Modelo | Formato | Resolución crop | Tarea |
|--------|---------|----------------|-------|
| OCR Lote | .rknn (INT8) | 320×64 | Caracteres en relieve |
| Defectos | .rknn (INT8) | 224×224 | Rayones, abolladuras |
| Presencia | .rknn (INT8) | 112×112 | Etiqueta presente/ausente |

## Próximos Pasos

1. Benchmark de latencia: medir tiempo real de inferencia .rknn en RV1106
   para cada crop al tamaño objetivo.
2. Integración con el GPIO trigger (fotocelda → captura → pipeline → resultado
   antes del próximo trigger).
3. Calibración colorimétrica: matriz 3×3 de transformación de cámara a
   espacio CIE L*a*b* usando target de calibración físico.
