# Proposal: Vision Pipeline C++ Contracts (Fase 1)

## Intent
Diseñar y sellar los contratos abstractos del pipeline de inferencia visual industrial en C++ moderno, aplicando RAII + Dependency Injection + Producer-Consumer, para el Rockchip RV1106. El objetivo es tener un diseño testeable en PC (SIL) antes de tocar la NPU real.

## Scope
- 5 archivos de cabecera (.hpp) en `edge_ops/gema_vision/include/`
- Sin archivos .cpp — solo contratos puros
- Namespace `gema::vision`
- Branch: `feature/gema-vision`

## No scope
- Implementación de la NPU (RknnContext .cpp)
- Implementación del GPIO (FlashTrigger .cpp)
- Integración con OpenCV en tiempo real
- Compilación cruzada para RV1106

## Approach
1. `VisualPrimitive` struct con bbox, confidence, class_id, roi (zero-copy cv::Mat header)
2. `InferenceEngine` interfaz virtual pura para inyectar RknnContext o MockEngine
3. `FlashTrigger` interfaz virtual pura para GPIO edge-triggered
4. `ThreadSafeQueue<T>` cola concurrente template con push/pop/try_pop/shutdown
5. `InferenceOrchestrator` orquestador con DI + consumer thread + start()/stop()
