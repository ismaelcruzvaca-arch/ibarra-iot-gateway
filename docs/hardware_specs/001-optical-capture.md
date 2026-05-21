# Especificación de Captura Óptica — Arquitectura Multi-Cabeza

**Estado:** Aprobada
**Fecha:** 2026-05-21
**Contexto:** Líneas de empaque — inspección de envases metálicos y relieves
en aluminio brillante con posible presencia de polvo de chocolate.

---

## 1. Decisión Arquitectónica

Se **abandona** el enfoque de "una cámara universal" que pretendía resolver
todos los casos de inspección con un solo sensor y lente. En su lugar, la
estación usará una **arquitectura Multi-Cabeza** donde cada cámara está
especializada para una tarea específica (OCR, colorimetría, presencia de
etiqueta, etc.).

### Sensores

| Sensor | Tipo | Shutter | Uso principal |
|--------|------|---------|---------------|
| IMX296 (Sony) | Global Shutter | Electrónico | OCR en relieves, código de lote |
| AR0234CS (onsemi) | Global Shutter | Electrónico | Visión general, presencia, defectos |

Ambos sensores son **Global Shutter** — no tienen rolling shutter, lo que
elimina la distorsión por vibración o movimiento de la línea.

> **Justificación:** Rolling shutter produce distorsión temporal en objetos
> en movimiento. En una línea de empaque a velocidad constante, cualquier
> vibración o micro-movimiento del envase genera artefactos que un clasificador
> de IA interpreta erróneamente como defectos. Global Shutter congela la escena.

---

## 2. Trigger por Hardware

El disparo de captura debe venir exclusivamente de **una fotocelda industrial**
conectada a los **pines GPIO** del RV1106, **no** por software ni por temporizador.

### Secuencia

1. El envase intercepta el haz de la fotocelda.
2. La fotocelda envía una señal TTL (3.3V/5V) al GPIO del RV1106.
3. El GPIO activa simultáneamente:
   - La cámara (captura de cuadro).
   - El **Flash Estroboscópico** (duración sub-milisegundo).
4. El flash congela cualquier movimiento residual y proporciona luz
   suficiente para una exposición corta.

### Requisitos del Flash

- Duración: < 1 ms (ideal: 10–50 μs).
- Sincronización: El RV1106 debe poder generar una señal de trigger
  con latencia determinista (ideal: < 100 μs entre GPIO y flash).
- Potencia: Suficiente para iluminar el campo de visión a la distancia
  de trabajo con la apertura y exposición seleccionadas.

---

## 3. Iluminación

Se definen **dos regímenes de iluminación** independientes, cada uno
optimizado para una tarea de inspección diferente.

### 3.1 Low-Angle Dark Field (OCR en relieves)

- **Técnica:** Luz monocromática (ej. roja 630 nm)入射 rasante (< 15°)
  sobre la superficie del envase.
- **Polarización:** Cruzada (emisor + filtro polarizador en la cámara
  rotados 90°) para eliminar reflejos especulares del aluminio brillante.
- **Propósito:** Crear contraste máximo en relieves (códigos de lote,
  fechas de caducidad) donde la luz rasante proyecta sombras en las
  hendiduras pero se refleja uniformemente en las superficies planas.
- **Resultado:** Una imagen donde los caracteres en relieve aparecen
  oscuros sobre fondo claro (o viceversa), ideal para OCR.

### 3.2 Dome Lighting Difuso (Colorimetría)

- **Técnica:** Cúpula hemisférica difusa con LEDs blancos de alta CRI
  (> 90, ideal > 95).
- **Propósito:** Iluminación uniforme y sin sombras para evaluación
  de color, presencia de etiqueta, y detección de manchas.
- **Resultado:** Una imagen con iluminación omnidireccional que elimina
  brillos especulares y permite medir color verdadero (L*a*b*).

> **Importante:** Ambos sistemas de iluminación deben poder activarse
> de forma independiente o simultánea, controlados por GPIO. La
> separación física evita contaminación lumínica entre regímenes.
