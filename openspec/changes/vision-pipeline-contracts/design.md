# Design: Vision Pipeline C++ Contracts

## Architecture Diagram

```
                      GPIO Pin (24V → Opto → 3.3V)
                            │
                     FlashTrigger (interfaz)
                            │  wait_for_flash()
                            ▼
                   [Hilo Productor - Alta Prioridad]
                            │
                    capture_frame()
                            │
                    ThreadSafeQueue<cv::Mat>
                            │  push()
                            ▼
                   [Hilo Consumidor - Inferencia]
                            │
              InferenceOrchestrator (consumer_loop)
                            │
              ┌───────────────┼────────────────┐
              ▼               ▼                ▼
     InferenceEngine    Post-Process      MqttClient
     (interfaz)         (OCR/Color)       (wrapper existente)
         │                                    │
    ┌────┴────┐                        JSON/Protobuf
    ▼         ▼                            │
 RknnContext MockEngine                    ▼
 (NPU real)  (SIL tests)               Hasura/Nhost
```

## RAII Ownership
```
RknnContext rknn("model.rknn");   // ctor: allocate NPU memory
// ... use ...
// dtor (~RknnContext): FREE memory, destroy tensors, close NPU handle
// GUARANTEED even if exception is thrown
```

## Producer-Consumer Thread Model
- **Producer**: GPIO ISR → `capture_frame()` → `queue.push(frame)`
- **Consumer**: `queue.pop()` → `engine.infer(frame)` → `mqtt.publish(result)`
- Queue is UNBOUNDED (max 8 frames headroom at 1.5 Hz)
- `std::condition_variable` wakes consumer, zero CPU spin

## Dependency Injection Graph
```
InferenceOrchestrator
  ├── InferenceEngine&     ← inyectada (RknnContext | MockEngine)
  ├── ThreadSafeQueue&     ← inyectada (compartida con producer)
  └── MqttClient&          ← inyectada (wrapper gateway_translator)
```

## RAII Safety Net
| Recurso | Adquirido en | Liberado en |
|---------|--------------|-------------|
| NPU tensores | RknnContext ctor | ~RknnContext dtor |
| GPIO fd | FlashTrigger ctor | ~FlashTrigger dtor |
| Thread | start() | stop() (join) |
| MQTT connection | MqttClient ctor | ~MqttClient dtor |

## Visual Primitive Class Map
| class_id | Significado | Post-procesamiento |
|----------|-------------|-------------------|
| 0 | DEFECT | Contar + loguear + alertar |
| 1 | OCR_ZONE | Recorte ROI → Tesseract / RKNN OCR |
| 2 | COLOR_ZONE | Recorte ROI → HSV histogram |
| 3+ | Reservado | Aplicación específica |
