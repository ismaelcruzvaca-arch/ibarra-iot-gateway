# Specs: Vision Pipeline C++ Contracts

## 1. VisualPrimitive
- El struct DEBE contener `cv::Rect bbox`, `float confidence`, `int class_id`, `cv::Mat roi`
- El campo `roi` DEBE ser zero-copy (cv::Mat header, no clonar píxeles)
- `PrimitiveBatch` DEBE ser alias de `std::vector<VisualPrimitive>`

## 2. InferenceEngine (interfaz)
- `PrimitiveBatch infer(const cv::Mat& frame)` — virtual pura
- `bool load_model(const std::string& path)` — virtual pura
- Destructor virtual default
- NO DEBE lanzar excepciones en frame vacío (devuelve vector vacío)

## 3. FlashTrigger (interfaz)
- `void wait_for_flash()` — virtual pura, bloqueante
- DEBE soportar señal de cancelación para shutdown limpio
- Latencia objetivo: microsegundos (GPIO edge-triggered)

## 4. ThreadSafeQueue<T>
- `void push(T&&)` / `void push(const T&)` — thread-safe, no bloqueante
- `T pop()` — bloqueante con condition_variable
- `std::optional<T> try_pop()` — no bloqueante
- `void shutdown()` — despierta todos los waiters
- `bool empty()` / `size_t size()` — consulta
- NO copiable, NO movable

## 5. InferenceOrchestrator
- Constructor con DI: `InferenceEngine&`, `ThreadSafeQueue<cv::Mat>&`, `MqttClient&`
- `void start()` — lanza consumer thread
- `void stop()` — join graceful
- `bool is_running()` / `uint64_t frames_processed()` — status
