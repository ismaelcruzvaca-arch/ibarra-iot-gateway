# Tasks: Vision Pipeline C++ Contracts

## Fase 1 — Contratos Abstractos (Headers)

- [x] 1.1 Crear `edge_ops/gema_vision/include/visual_primitive.hpp`
  - struct VisualPrimitive con cv::Rect bbox, float confidence, int class_id, cv::Mat roi
  - alias PrimitiveBatch = std::vector<VisualPrimitive>
  - namespace gema::vision

- [x] 1.2 Crear `edge_ops/gema_vision/include/inference_engine.hpp`
  - Interfaz virtual pura InferenceEngine
  - virtual PrimitiveBatch infer(const cv::Mat& frame) = 0
  - virtual bool load_model(const std::string& path) = 0
  - Destructor virtual default

- [x] 1.3 Crear `edge_ops/gema_vision/include/flash_trigger.hpp`
  - Interfaz virtual pura FlashTrigger
  - virtual void wait_for_flash() = 0
  - Documentación de timing contract (microsegundos, GPIO)

- [x] 1.4 Crear `edge_ops/gema_vision/include/thread_safe_queue.hpp`
  - Template class ThreadSafeQueue<T>
  - push() (rvalue + const-ref), pop() bloqueante, try_pop()
  - shutdown(), empty(), size()
  - Non-copiable, non-movable
  - std::mutex + std::condition_variable

- [x] 1.5 Crear `edge_ops/gema_vision/include/inference_orchestrator.hpp`
  - Forward declaration de MqttClient
  - struct InferenceResult con primitives + counts
  - class InferenceOrchestrator con DI completa
  - start() / stop() / is_running() / frames_processed()

- [x] 1.6 Commit: `feat: Definir contratos abstractos puros para el pipeline de visión C++`

## Fase 2 — SIL Tests + Mocks

- [x] 2.1 Implementar `include/mock_engine.hpp`
  - Header-only, inline. load_model() → true, infer() → sleep(20ms) + 2 primitivas estáticas

- [x] 2.2 Implementar `include/mock_flash_trigger.hpp`
  - Header-only, inline. wait_for_flash() → sleep(666ms) ≈ 90 golpes/min

- [x] 2.3 Implementar `src/inference_orchestrator.cpp`
  - consumer_loop con patrón de drenado (sale solo cuando pop() devuelve frame vacío tras shutdown)
  - JSON mínimo serializado y publicado por MQTT

- [x] 2.4 Crear `test/test_vision_pipeline.cpp`
  - 4 tests GTest: ConsumerProcessesAllFrames ✅, NoFramesNoCrash ✅, MockEngineReturnsTwoPrimitives ✅, DoubleStartStopIsIdempotent ✅
  - 100% passed, 0 failed

- [x] 2.5 Crear `CMakeLists.txt` + `Dockerfile.test`
  - Build y test en container Ubuntu 22.04 con GTest + OpenCV

- [x] 2.6 Commit: `test: Implementar entorno SIL con Mocks para el pipeline de visión C++`

## Fase 3 — Pendiente
- [ ] Implementar RknnContext (NPU real)
- [ ] Implementar FlashTrigger (GPIO /dev/gpiochip)
- [ ] Integración con cámara real (V4L2)
- [ ] Benchmark de throughput en RV1106
