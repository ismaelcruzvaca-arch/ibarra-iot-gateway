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

## Fase 2 — Pendiente (próximo sprint)
- [ ] Implementar MockEngine para SIL tests
- [ ] Implementar RknnContext (NPU real)
- [ ] Implementar FlashTrigger (GPIO /dev/gpiochip)
- [ ] Implementar consumer_loop en InferenceOrchestrator.cpp
- [ ] SIL tests con GTest
