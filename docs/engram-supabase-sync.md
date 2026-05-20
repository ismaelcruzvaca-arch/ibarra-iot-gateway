# Procedimiento de Sync Engram ↔ Cloud (Supabase)

> **ÚLTIMA ACTUALIZACIÓN:** 2026-05-20
> **AUTOR:** Open Code (auto-generado tras investigación)
> **PROYECTO:** ibarra-iot-gateway

---

## 📋 Resumen Ejecutivo

Este documento establece el procedimiento EXACTO y REPETIBLE para sincronizar las memorias persistentes de Engram (almacenadas localmente en SQLite) con la base de datos cloud de Supabase del proyecto **ibarra-iot-gateway**. **Nunca más adivinar.**

---

## 🔧 Requisitos Previos

1. **Engram CLI instalado** en:
   `C:\Users\diego\AppData\Local\engram\bin\engram`

2. **Supabase CLI accesible** via `npx supabase` (v2.100.1+)

3. **Proyecto Supabase existente:**
   - Project ref: `zbnritimnflkgihbfahb`
   - Connection string: `postgresql://postgres.zbnritimnflkgihbfahb:<password>@aws-1-us-west-2.pooler.supabase.com:5432/postgres`

4. **Variables de entorno** requeridas (configurar ANTES de arrancar el servidor cloud):

   | Variable | Propósito |
   |----------|-----------|
   | `ENGRAM_DATABASE_URL` | Postgres DSN de Supabase para el servidor cloud |
   | `ENGRAM_CLOUD_ALLOWED_PROJECTS` | Lista de proyectos permitidos (ej: `ibarra-iot-gateway`) |
   | `ENGRAM_CLOUD_INSECURE_NO_AUTH=1` | Solo para desarrollo local sin autenticación |
   | `ENGRAM_CLOUD_TOKEN` | Bearer token (si se requiere autenticación) |
   | `ENGRAM_JWT_SECRET` | Secreto JWT (obligatorio si se usa `ENGRAM_CLOUD_TOKEN`) |

5. **Proyecto enrolado** en Engram (verificado con `engram cloud status`)

---

## 🚀 Procedimiento Paso a Paso

### Paso 0: Preparar entorno

```powershell
# Ir al directorio del proyecto
cd "C:\Users\diego\Downloads\PROYECTO GEMA 2026\IOT Produccion Ibarra\ibarra-iot-gateway"

# Cargar variables de entorno (ejemplo)
$env:ENGRAM_CLOUD_ALLOWED_PROJECTS = "ibarra-iot-gateway"
$env:ENGRAM_CLOUD_INSECURE_NO_AUTH = "1"
# $env:ENGRAM_DATABASE_URL = "postgresql://..."  # ← TU connection string de Supabase
```

> **⚠️ IMPORTANTE:** `ENGRAM_CLOUD_ALLOWED_PROJECTS` y `ENGRAM_CLOUD_INSECURE_NO_AUTH=1` (o `ENGRAM_CLOUD_TOKEN` + `ENGRAM_JWT_SECRET`) son **obligatorios** para que `engram cloud serve` arranque. Sin ellos el servidor falla.

---

### Paso 1: Vincular proyecto Supabase local

```powershell
# Linkear el proyecto Supabase existente
npx supabase link --project-ref <TU_PROJECT_REF>
# Te pedirá la Database Password si el proyecto tiene enabled
```

> **¿No sabés el project ref?** Está en la URL de tu proyecto Supabase:
> `https://supabase.com/dashboard/project/<project-ref>`

**Output esperado:**
```
Finished supabase link.
```

---

### Paso 2: Verificar estado del servidor cloud Engram

```powershell
# Verificar si el servidor cloud de Engram ya está corriendo
netstat -ano | findstr 8080
```

- **Si hay resultado** (`LISTENING` en puerto 8080) → ir al **Paso 4**
- **Si NO hay resultado** → ir al **Paso 3**

---

### Paso 3: Levantar servidor cloud de Engram

```powershell
# Asegurarse de tener las variables de entorno configuradas PRIMERO
$env:ENGRAM_CLOUD_ALLOWED_PROJECTS = "ibarra-iot-gateway"
$env:ENGRAM_CLOUD_INSECURE_NO_AUTH = "1"  # Solo para local/inseguro
# $env:ENGRAM_DATABASE_URL = "postgresql://..."  # ← TU DSN de Supabase

# Método A: Ventana visible (para debug)
& "C:\Users\diego\AppData\Local\engram\bin\engram.exe" cloud serve

# Método B: Background (producción / sesiones largas)
Start-Process -FilePath "C:\Users\diego\AppData\Local\engram\bin\engram.exe" `
  -ArgumentList "cloud", "serve" `
  -WindowStyle Hidden

# Verificar después de 5-10 segundos
netstat -ano | findstr 8080
```

**Output esperado del servidor (modo visible):**
```
Engram Cloud Dashboard: http://127.0.0.1:8080
Cloud API: http://127.0.0.1:8080/api
```

---

### Paso 4: Verificar estado cloud

```powershell
& "C:\Users\diego\AppData\Local\engram\bin\engram.exe" cloud status
```

**Salida esperada:**
```
Cloud status: configured (target=cloud)
Server: http://127.0.0.1:8080
Auth status: ready (insecure mode enabled)
Sync readiness: ready for explicit --project sync (project must be enrolled)
```

Si el proyecto **no está enrolado**, ver el **Paso 4b**.

---

### Paso 4b: Enrolar proyecto en cloud (solo la primera vez)

```powershell
# Enrolar el proyecto ibarra-iot-gateway
# (solo necesario si cloud status no lo muestra como enrolado)
# NOTA: enroll usa argumento posicional, NO flag --project
& "C:\Users\diego\AppData\Local\engram\bin\engram.exe" cloud enroll ibarra-iot-gateway
```

---

### Paso 5: Ejecutar sync

```powershell
& "C:\Users\diego\AppData\Local\engram\bin\engram.exe" `
  sync --cloud --project ibarra-iot-gateway
```

**⚠️ IMPORTANTE:** `--all` NO está soportado con `--cloud`. Siempre especificar `--project` explícitamente.

**Salida esperada:**
```
Exporting memories for project "ibarra-iot-gateway" to cloud...
Created chunk XXXXXXXX
  Sessions:     N
  Observations: N
  Prompts:      N
  Mutations:    N
Cloud sync complete for project "ibarra-iot-gateway".
```

---

### Paso 6: Verificar estado post-sync

```powershell
& "C:\Users\diego\AppData\Local\engram\bin\engram.exe" sync --status
```

**Salida esperada:**
```
Sync status:
  Local chunks:    N
  Remote chunks:   N
  Pending import:  0   ← Debe ser 0 cuando todo está sincronizado
```

---

### Paso 7: Verificar proyectos registrados

```powershell
& "C:\Users\diego\AppData\Local\engram\bin\engram.exe" projects list
```

**Salida esperada:**
```
Projects (N):
  ibarra-iot-gateway              X obs     X sessions    X prompts
```

---

## 🏗️ Arquitectura del Sistema

```
┌─────────────────────────────────────────────────────────────────┐
│  OpenCode Agent (Plugin engram.ts)                              │
│  ├─ Comunica con servidor local engram: 127.0.0.1:7437         │
│  ├─ Guarda memoria vía HTTP calls                               │
│  └─ SQLite local como almacenamiento primario                   │
└──────────────────────┬──────────────────────────────────────────┘
                       │
                       │ HTTP API (puerto 7437)
                       │
┌──────────────────────▼──────────────────────────────────────────┐
│  Engram Local Server (`engram serve`)                           │
│  ├─ Base de datos SQLite local                                  │
│  ├─ Expone API HTTP para lectura/escritura                      │
│  └─ Sync automático con .engram/ en el repo                     │
└──────────────────────┬──────────────────────────────────────────┘
                       │
                       │ `engram sync --cloud --project ibarra-iot-gateway`
                       │
┌──────────────────────▼──────────────────────────────────────────┐
│  Engram Cloud Server (`engram cloud serve`)                     │
│  ├─ Corre en: 127.0.0.1:8080                                    │
│  ├─ Conecta con Supabase via `ENGRAM_DATABASE_URL`              │
│  └─ Materializa mutaciones en PostgreSQL                        │
└──────────────────────┬──────────────────────────────────────────┘
                       │
                       │ Conexión persistente a Supabase
                       │
┌──────────────────────▼──────────────────────────────────────────┐
│  Supabase (proyecto ibarra-iot-gateway)                         │
│  └─ Tablas: sessions, observations, prompts, chunks, etc.      │
└─────────────────────────────────────────────────────────────────┘
```

---

## 🔍 Troubleshooting

| Síntoma | Causa probable | Solución |
|---------|---------------|----------|
| `dial tcp 127.0.0.1:8080: connectex: No connection could be made` | Servidor cloud no está corriendo | Ejecutar `engram cloud serve` (ver Paso 3) |
| `cloud sync requires a single explicit --project scope; --all is not supported` | Usando `--all` con `--cloud` | Especificar `--project ibarra-iot-gateway` |
| `Server NOT running on port 8080` | El servidor cloud murió o no arrancó | Verificar procesos con `Get-Process \| Where-Object { $_.ProcessName -like "*engram*" }` |
| `Pending import: N` (N > 0) | Hay chunks locales no sincronizados | Ejecutar sync explícito para el proyecto |
| `cloud serve` no arranca / error silencioso | Faltan `ENGRAM_CLOUD_ALLOWED_PROJECTS` o `ENGRAM_CLOUD_INSECURE_NO_AUTH` | Verificar que las env vars estén configuradas ANTES de arrancar |
| Múltiples procesos engram | Sesiones anteriores no cerradas | Matar procesos viejos: `Get-Process \| Where-Object { $_.ProcessName -like "*engram*" } \| Stop-Process` |
| `Failed to connect to database` | `ENGRAM_DATABASE_URL` incorrecta o Supabase no accesible | Verificar connection string desde Supabase Dashboard → Project Settings → Database |

---

## ⚡ Comandos Rápidos (Copiar y Pegar)

### Sync completo en un bloque:

```powershell
# 1. Setear env vars (ajustar DATABASE_URL)
$env:ENGRAM_CLOUD_ALLOWED_PROJECTS = "ibarra-iot-gateway"
$env:ENGRAM_CLOUD_INSECURE_NO_AUTH = "1"
# $env:ENGRAM_DATABASE_URL = "postgresql://postgres:xxxx@aws-0-us-west-1.pooler.supabase.com:6543/postgres"

# 2. Verificar servidor cloud
$portCheck = netstat -ano | Select-String "8080"
if (-not $portCheck) {
    Write-Output "Servidor NO corriendo. Levantando..."
    Start-Process -FilePath "C:\Users\diego\AppData\Local\engram\bin\engram.exe" -ArgumentList "cloud", "serve" -WindowStyle Hidden
    Start-Sleep 10
} else {
    Write-Output "Servidor ya corriendo en 8080"
}

# 3. Sync
& "C:\Users\diego\AppData\Local\engram\bin\engram.exe" sync --cloud --project ibarra-iot-gateway

# 4. Verificar estado
& "C:\Users\diego\AppData\Local\engram\bin\engram.exe" sync --status
```

---

## 📝 Notas Críticas

1. **NO MODIFICAR la base de datos de Supabase directamente.** El sync es unidireccional: local → cloud. Engram maneja las mutaciones automáticamente.

2. **El servidor cloud (`engram cloud serve`) debe permanecer activo** durante todo el sync. Si se cierra, el sync fallará.

3. **El proyecto debe estar enrolado.** Verificar con `engram cloud status` que el proyecto esté listo para sync. Si no, ejecutar `engram cloud enroll ibarra-iot-gateway` (sin `--project`).

4. **Las variables de entorno se configuran ANTES de arrancar el servidor.** Si arrancaste el servidor sin ellas, detenelo, configuralas y volvé a arrancar.

5. **Las memorias locales (SQLite) son la fuente de verdad.** El cloud es un backup replicado. Si hay conflictos, Engram resuelve las mutaciones automáticamente.

6. **Después de compaction:** Siempre ejecutar sync para preservar las memorias de la sesión compactada.

---

## 🔄 Checklist de Cierre de Sesión

- [ ] Cambios commiteados y pusheados
- [ ] `mem_session_summary` guardado en Engram local
- [ ] Servidor cloud de Engram verificado/levantado (`netstat -ano | findstr 8080`)
- [ ] Variables de entorno cargadas (`ENGRAM_CLOUD_ALLOWED_PROJECTS`, etc.)
- [ ] Sync ejecutado: `engram sync --cloud --project ibarra-iot-gateway`
- [ ] Sync status verificado: `Pending import: 0`
- [ ] Este documento actualizado si el procedimiento cambió

---

## 📎 Referencias

- **Engram CLI:** `C:\Users\diego\AppData\Local\engram\bin\engram.exe`
- **Supabase CLI:** `npx supabase` (v2.100.1+)
- **Supabase Dashboard:** https://supabase.com/dashboard
- **Proyecto Engram local:** `ibarra-iot-gateway` (`engram projects list`)
- **Plugin OpenCode:** `C:\Users\diego\.config\opencode\plugins\engram.ts` (si existe)
- **Convenciones:** `C:\Users\diego\.config\opencode\skills\_shared\engram-convention.md` (si existe)
