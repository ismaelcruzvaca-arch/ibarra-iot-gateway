# Especificación de Blindaje Ambiental y Termodinámica

**Estado:** Aprobada
**Fecha:** 2026-05-21
**Contexto:** Líneas de empaque en Guadalajara (~1,550 msnm) con presencia
de polvo de chocolate, vapor, y vibración industrial.

---

## 1. Gabinete y Sello Ambiental

Todos los componentes electrónicos deben alojarse en gabinetes que cumplan:

| Estándar | Descripción |
|----------|-------------|
| **IP67** | Protección total contra polvo + inmersión temporal (1 m, 30 min) |
| **IP69K** | Protección contra chorros de agua a alta presión y alta temperatura (lavado CIP) |

### Válvulas de Respiración (Pressure Equalization)

Los gabinetes IP67/IP69K sellados herméticamente sufren diferenciales de
presión por cambios de temperatura (ciclos día/noche, lavado con agua
caliente). Sin una vía de ecualización:

1. El gabinete succiona aire húmedo al enfriarse → condensación interna.
2. La junta se fatiga por ciclos repetidos de presión/vacío.

**Solución:** Instalar **válvulas de respiración con membrana** (ej. Gore
Protective Vents o equivalente) que:
- Ecualizan la presión sin permitir entrada de agua/polvo.
- Mantienen la clasificación IP del gabinete.
- Tienen vida útil > 5 años en ambiente industrial.

---

## 2. Sistema Air Knife / Air Purge

El cristal frontal de la ventana de inspección es el punto más crítico de
contaminación. El polvo de chocolate (grasa + cacao) se adhiere
electrostática y mecánicamente al vidrio.

### Especificación

- **Air Knife:** Boquilla de cortina de aire comprimido montada sobre el
  cristal, expulsando una lámina de aire continua o pulsada.
- **Air Purge:** Pequeño flujo positivo de aire seco dentro del compartimento
  óptico, manteniendo presión positiva para evitar entrada de polvo.
- **Calidad del aire:** Filtrado a 0.3 μm (HEPA o similar) — el aire sin
  filtrar contiene aceite del compresor que emulsiona en el vidrio.

> **Regla de diseño:** El Air Knife debe activarse antes de que la línea
> arranque y mantenerse durante toda la producción. No debe depender de
> sensores de contaminación — es preventivo, no reactivo.

---

## 3. Mitigación Térmica (Altitud 1,550 msnm)

A 1,550 msnm la densidad del aire es ~15% menor que a nivel del mar.
Esto reduce drásticamente la eficiencia del enfriamiento por convección
forzada (ventiladores).

### Prohibición: Enfriamiento por Aire Pasivo Interno

- No usar ventiladores dentro del gabinete óptico.
- No usar heatsinks con flujo de aire interno.
- **Razón:** El aire enrarecido disipa ~30% menos calor que a nivel del mar.
  Un ventilador interno movería aire caliente sobre los componentes sin
  poder extraerlo efectivamente, creando puntos calientes.

### Solución: Thermal Bridge a la Carcasa

El calor generado por el **RV1106** (SoC principal, ~5-7 W TDP) debe
transferirse directamente a la **carcasa de aluminio externa**, que actúa
como disipador masivo al aire ambiente.

```
   RV1106 (chip)
       │
   Thermal Pad (Gap Filler)
       │
   Carcasa de Aluminio (externo)
       │
   Aire ambiente (convección natural)
```

### Especificación del Thermal Pad

| Parámetro | Requisito |
|-----------|-----------|
| Tipo | Gap Filler no-silicónico |
| Conductividad térmica | ≥ 3.0 W/m·K |
| Espesor | 1.0–2.0 mm (según gap mecánico) |
| Outgassing TML (%) | < 0.5% (ASTM E595) |
| Outgassing CVCM (%) | < 0.05% (ASTM E595) |

> **Razón del no-silicónico:** Los pads de silicón convencionales emiten
> compuestos orgánicos volátiles (outgassing) con el calor. En un
> compartimento óptico sellado, estos vapores se condensan en la lente
> y el sensor, causando empañamiento irreversible. Los gap fillers
> no-silicónicos (basados en poliuretano o acrílico termoconductor)
> eliminan este riesgo.

---

## 4. Mitigación de Vibración

Las líneas de empaque generan vibración de baja frecuencia (5–50 Hz)
por motores, transportadores y compresores.

### Aislamiento Elastomérico

- Montar la placa portacámaras sobre soportes elastoméricos (caucho
  natural o neopreno) con frecuencia natural < 5 Hz.
- El material debe ser resistente a grasas y aceites industriales.
- Evitar montajes rígidos directos al chasis de la línea.

---

## 5. Protección EMC/ESD en Red Modbus

El bus Modbus RTU (RS-485) recorre toda la línea de empaque y es
susceptible a descargas electrostáticas y acoplamiento electromagnético
de motores.

### Especificación

- **Optoaisladores** en cada nodo Modbus (no acoplamiento magnético).
- Aislamiento mínimo: 2.5 kV RMS entre bus y lógica.
- Terminación: Resistencias de 120 Ω en ambos extremos del bus.
- Cable: Par trenzado blindado (STP) con drenaje a tierra en un solo punto.
- Protección TVS (Transient Voltage Suppression) en cada conector RS-485.
