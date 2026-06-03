# HARDWARE SECURITY — BCM2712 & RPi5 Industrial Gateway

## 1. eFuses y Secure Boot V2 en el BCM2712

El SoC **BCM2712** de la Raspberry Pi 5 expone **OTP (One-Time Programmable) fuses** que controlan el arranque seguro. Estas fuses se programan irreversiblemente desde el bootloader EEPROM y no pueden ser modificadas por software en runtime.

### Mecánica de Secure Boot V2

1. **ROM de arranque (BootROM)** — El BCM2712 contiene una ROM inmutable que es la primera en ejecutarse al aplicar energía.
2. **Validación del bootloader** — Si los OTP de secure boot están programados, la ROM rechaza cualquier bootloader (`bootcode4.bin`, `start4.elf`) que no esté firmado criptográficamente.
3. **Cadena de confianza** — El bootloader firmado valida a su vez el kernel (`kernel8.img`) y el Device Tree (`bcm2712-rpi-5.dtb`) antes de ceder el control.
4. **OTP bits clave**:
   - `BOOT_MODE` — Bloquea el modo de arranque (solo SD/eMMC, no USB ni GPIO)
   - `DISABLE_JTAG` — Desconecta permanentemente la interfaz JTAG del SoC
   - `LOCK` — Impide nuevas escrituras OTP, fosilizando el estado de seguridad
   - `SD_BOOT` — Fuerza arranque desde tarjeta SD/eMMC bloqueando alternativas

### Limitaciones conocidas

- El Secure Boot del BCM2712 no es un HSM (Hardware Security Module). Es un **Secure Boot por firmware**, no por hardware dedicado.
- Para protección criptográfica real se necesita cifrado a nivel de aplicación o un ATECC608B externo (Trust&GO).
- Las OTP deben programarse en un entorno controlado (USB de recovery), idealmente en un fixture de producción.

---

## 2. Particionado A/B con `tryboot`

Raspberry Pi OS soporta **actualizaciones atómicas A/B** mediante el mecanismo `tryboot` en el bootloader EEPROM. Esto permite actualizar el sistema de raíz con rollback automático.

### Esquema de particiones

```
/dev/mmcblk0p1 → /boot/firmware   (bootloader EEPROM config, 512 MB)
/dev/mmcblk0p2 → rootfs_A         (partición activa primaria)
/dev/mmcblk0p3 → rootfs_B         (partición secundaria para OTA)
/dev/mmcblk0p4 → /data            (partición persistente compartida)
```

### Flujo `tryboot`

1. El sistema arranca desde `rootfs_A` (partición activa por defecto).
2. Se descarga una actualización OTA y se escribe en `rootfs_B`.
3. Se modifica `/boot/firmware/config.txt` añadiendo:
   ```
   [tryboot]
   tryboot=1
   boot_partition=3
   ```
4. Se reinicia el sistema. La EEPROM lee `tryboot=1` e intenta arrancar desde `rootfs_B`.
5. **Si el arranque falla** (kernel panic, watchdog timeout, etc.), el bootloader incrementa `tryboot_fail_count` y revierte automáticamente a `rootfs_A` en el siguiente reinicio. No requiere intervención manual.
6. **Si el arranque es exitoso**, la aplicación marca la partición como permanente escribiendo `tryboot=0` en `config.txt`. La próxima OTA escribirá sobre `rootfs_A`.
7. Si `tryboot_fail_count` supera un umbral (ej: 3), el bootloader bloquea `tryboot` y envía una alerta.

### Idempotencia en Ansible

El playbook NO debe gestionar particiones A/B directamente — esa responsabilidad es del sistema OTA. El aprovisionamiento inicial solo crea la tabla de particiones y asegura que `config.txt` tenga `tryboot` habilitado:

```
# En /boot/firmware/config.txt (solo aprovisionamiento inicial)
tryboot_auto_reboot=1
tryboot_fail_count_max=3
```

---

## 3. Supercondensadores vs. Baterías de Litio para Apagado Seguro

### Contexto industrial

Un gateway IoT industrial (RPi5 + RP1 + periféricos) pierde alimentación en planta por:
- Conmutación de fases en tableros eléctricos (microcortes de 50-500 ms)
- Desconexión intempestiva por mantenimiento
- Caídas de red por motores de alta potencia

En todos los casos, el sistema debe ejecutar un **apagado seguro** (~5 segundos): flush de SQLite WAL, publish de LWT MQTT `OFFLINE`, y unmount de filesystems.

| Requisito | Batería LiPo/LiFePO4 | Supercondensador |
|-----------|----------------------|------------------|
| Ciclos de vida | 300–500 (2-3 años industrial) | **500.000+** (10-15 años) |
| Rango térmico | 0 °C a 45 °C (carga) | **-40 °C a 65 °C** |
| Riesgo térmico | Inflamable si se perfora o sobrecarga | **Ninguno** (químicamente inerte) |
| Corriente de pico | Limitada por C-rate | **Muy alta** (ESR ultrabajo) |
| Autodescarga | ~5 % mensual | ~20 % mensual |
| Mantenimiento | Reemplazo cada 2-3 años | **Cero mantenimiento** |
| Costo a 10 años | Alto (reemplazos + mano de obra) | **Menor** (instalación única) |
| Tamaño en planta | Similar (2x 18650 vs 2x 100F/2.7V) | Comparable en PCB |

### Arquitectura propuesta

```
Alimentación 5V ──┬── Diodo OR ──┬── RPi5 (BCM2712 + RP1)
                   │              │
                   │              └── Regulador 5V ── Banco de supercaps (2x 100F/2.7V en serie = 50F/5.4V)
                   │
                   └── Controlador PowerGood ── GPIO RPi5 (detección de pérdida)
```

- **50F / 5.4V** proporciona ~15 Joules, suficientes para 5–8 segundos de apagado seguro (~3W de consumo RPi5 + periféricos).
- El GPIO de "PowerGood" dispara una interrupción que inicia el script de apagado.
- Con `dtoverlay=gpio-poweroff` en `config.txt` se asegura que el PMIC del BCM2712 corte la alimentación solo después de que el watchdog se dispare o el sistema complete el shutdown.

### Conclusión

**No hay discusión técnica**: en entorno industrial de manufactura (temperatura, vibración, humedad, vida útil >10 años), los supercondensadores son la única opción viable. Las baterías de litio solo se justifican en productos de consumo con vida útil <3 años y ambiente controlado.
