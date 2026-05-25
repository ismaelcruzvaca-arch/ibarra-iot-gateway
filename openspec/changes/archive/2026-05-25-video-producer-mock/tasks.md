# Tasks: VideoProducer + Mock — Shared-ptr FramePool Pipeline

## Phase 1 — Foundation (Pool + Queue)

- [x] 1.1 Add `FramePool::acquire_shared()` in `include/frame_pool.hpp` — returns `std::shared_ptr<cv::Mat>` with custom deleter calling `release(index)`. Backward compat: keep raw `acquire()` untouched.
- [x] 1.2 Verify `ThreadSafeQueue<T>` template needs no changes — only instantiation sites change the type param. Template is already generic.
- [x] 1.3 Update `test/test_vision_pipeline.cpp` — change `ThreadSafeQueue<cv::Mat>` to `ThreadSafeQueue<std::shared_ptr<cv::Mat>>`, use `std::make_shared<cv::Mat>` for pushes, dereference with `*frame_ptr`.

## Phase 2 — Interface + Mock

- [x] 2.1 Create `include/video_producer.hpp` — `IVideoProducer` abstract interface: `start()`, `stop()`, `is_running()`, `set_frame_rate(double)`. Namespace `gema::vision`.
- [x] 2.2 Create `include/mock_video_producer.hpp` — `MockVideoProducer` header-only, 5 patterns (SOLID_COLOR, GRADIENT, NOISE, CHECKERBOARD, TEST_CARD), 1.5 Hz default, `sleep_interruptible()` with 100ms chunks, producer loop acquires from pool → paints → pushes to queue.
- [x] 2.3 Create `include/rga_video_producer.hpp` — Placeholder header with `IVideoProducer` interface, guarded by `#ifdef __arm__` at implementation level (stub for x86).
- [x] 2.4 Create `test/test_video_producer.cpp` — GTest for: FramePool `acquire_shared()` releases on scope exit; MockVideoProducer start/stop lifecycle; produces frames at ~1.5 Hz; pattern cycling (5 patterns via frame_index); double start is no-op; stop during sleep is responsive.

## Phase 3 — Integration

- [x] 3.1 Update `include/inference_orchestrator.hpp` and `src/inference_orchestrator.cpp` — queue type to `ThreadSafeQueue<std::shared_ptr<cv::Mat>>&`, consumer loop pops shared_ptr and derefs for `engine_.infer(*frame_ptr)`. No explicit `pool_.release()` needed. **(Already done by Phase 1 agent — verified correct.)**
- [x] 3.2 Wire `src/main.cpp` — create `FramePool(640, 480, CV_8UC3)`, `ThreadSafeQueue<shared_ptr<cv::Mat>>(8)`, create producer based on `USE_MOCK_PRODUCER` guard. Fix signal handler: remove `_Exit()` and `watchdog.stop()` — only set `g_shutdown`. Shutdown order: orchestrator → producer → watchdog.

## Phase 4 — Build

- [x] 4.1 Update `CMakeLists.txt` — add `test_video_producer` GTest target linked to `gema_vision`, add `USE_MOCK_PRODUCER` option.
- [x] 4.2 Verify all type signatures match, headers correct, no stale references. **(Compilation cannot be verified on Windows — deferred to Linux/Docker CI. See Task 4.2 notes.)**
