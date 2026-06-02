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

Se definen **tres regímenes de iluminación** independientes, cada uno
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

### 3.3 Coaxial Bright-Field (Tinta sobre Metalizado)

- **Técnica:** Luz coaxial azul 470 nm a través de un **beamsplitter 50:50**
  (placa) colocado a 45° del eje óptico. La cámara observa a través del
  beamsplitter mientras el LED ilumina en ángulo recto hacia la superficie.
  La trayectoria óptica es: LED → polarizador → beamsplitter → superficie →
  beamsplitter → analizador → sensor.
- **Polarización:** Cruzada — polarizador lineal (relación de extinción
  ≥1000:1) sobre el arreglo LED, analizador en el lente de la cámara, ejes
  rotados 90°. La luz reflejada por el metalizado mantiene su polarización
  y es bloqueada por el analizador; la luz absorbida/dispersada por la tinta
  pierde polarización y pasa parcialmente, generando contraste.
- **Propósito:** Leer códigos impresos con tinta sobre superficies
  metalizadas brillantes (empaques laminados, envases reflectivos). El azul
  470 nm se absorbe fuertemente en tintas oscuras mientras el metal
  reflectante lo devuelve casi íntegro — la polarización cruzada extingue
  el destello especular.
- **Longitud de onda:** Azul 470 nm (±10 nm). Seleccionado por máxima
  absorción en tintas de impresión industrial estándar (carbón negro,
  pigmentos orgánicos) y alta reflectividad del aluminio en ese rango.
- **Trigger:** GPIO2 del RV1106 (independiente de GPIO0 y GPIO1). El pulso
  estroboscópico se genera mediante MOSFET de canal N (ej. IRLZ44N) con
  buffer TTL (SN74LVC1G17) para conmutación limpia. Duración del pulso:
  50–200 µs.
- **Sensor primario:** IMX296 (global shutter, 1.58 MP). Comparte el sensor
  con el régimen 1 (Low-Angle Dark Field) mediante conmutación óptica o
  selección de trigger.
- **Sensor secundario:** AR0234CS (global shutter, 2.3 MP) — opcional para
  captura de campo más amplio si el código de tinta abarca múltiples caras
  del envase.

> **Nota técnica:** La eficiencia del beamsplitter 50:50 implica una pérdida
> del ~50% de la luz en cada paso (75% total ida y vuelta). Esto se compensa
> con mayor potencia del LED azul y la alta eficiencia cuántica del IMX296
> en 470 nm (~70% QE). La relación de contraste esperada (tinta vs metal)
> es ≥8:1 en condiciones nominales.

> **Referencia cruzada:** Ver especificación completa en
> `openspec/changes/optical-capture-enhancement/specs/coaxial-brightfield-illumination/spec.md`
> para escenarios detallados de aceptación, criterios de éxito y metodología
> de validación óptica.

### 3.4 Tabla Resumen de Regímenes de Iluminación

| # | Régimen | Sensor | GPIO | Longitud de onda | Estroboscopio | Propósito |
|---|---------|--------|------|-------------------|---------------|-----------|
| 1 | Low-Angle Dark Field | IMX296 | GPIO0 | Roja 630 nm | <1 ms | OCR en relieves (código de lote) |
| 2 | Dome Lighting Difuso | AR0234CS | GPIO1 | Blanca alto CRI (>90) | <1 ms | Colorimetría, presencia de etiqueta |
| 3 | Coaxial Bright-Field | IMX296 | GPIO2 | Azul 470 nm (±10 nm) | 50–200 µs | Tinta sobre metalizado (ink-OCR) |

> Los tres GPIOs se disparan en paralelo desde la misma fotocelda. Solo el
> régimen activo (seleccionado por configuración SKU en boot) habilita su
> MOSFET de strobe mediante una compuerta AND por GPIO. Esto garantiza
> independencia eléctrica sin latencia de conmutación software.

> **Importante:** Los tres sistemas de iluminación deben poder activarse
> de forma independiente o simultánea, controlados por GPIO. La
> separación física evita contaminación lumínica entre regímenes.
