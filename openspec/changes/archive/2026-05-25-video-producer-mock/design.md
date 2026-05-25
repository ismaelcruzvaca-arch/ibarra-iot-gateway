# Design: VideoProducer + Mock — Shared-ptr FramePool Pipeline

**Change**: `video-producer-mock`
**Date**: 2026-05-25
**Status**: Draft

---

## Architecture Overview

```
                    ┌──────────────────┐
                    │   FramePool      │
                    │  (4 buffers)     │
                    │  pre-allocated   │
                    └────────┬─────────┘
                             │ acquire_shared()
                             │ returns shared_ptr<cv::Mat>
                             ▼
                    ┌──────────────────┐
    start() ───────►│ IVideoProducer   │◄────── set_frame_rate()
                    │ (abstract)       │
                    └────────┬─────────┘
                             │
                    ┌────────┴─────────┐
                    │                  │
                    ▼                  ▼
           ┌──────────────┐  ┌──────────────────┐
           │MockVideoProd.│  │ RgaVideoProducer │
           │(header-only) │  │ (#ifdef __arm__) │
           └──────┬───────┘  └──────────────────┘
                  │ push(shared_ptr)
                  ▼
         ┌──────────────────┐
         │ ThreadSafeQueue  │
         │<shared_ptr<Mat>> │  max 8, drop-oldest
         └────────┬─────────┘
                  │ pop()
                  ▼
         ┌──────────────────┐
         │InferenceOrch.    │
         │ consumer_loop()  │
         │ processes *frame │
         │ shared_ptr goes  │
         │ out of scope →   │
         │ deleter calls    │
         │ pool_.release()  │
         └──────────────────┘
```

**Shutdown sequence** (reverse of start):
```
SIGTERM → g_shutdown = true → main loop exits
  1. orchestrator.stop()   — joins consumer thread
  2. producer.stop()       — joins producer thread
  3. watchdog.stop()       — disarms WDT
```

---

## Component Details

### 1. FramePool — `acquire_shared()` (NEW method)

```cpp
// include/frame_pool.hpp
std::shared_ptr<cv::Mat> acquire_shared() {
    size_t idx = acquire();  // existing atomic CAS logic
    cv::Mat* frame_ptr = &pool_[idx];
    return std::shared_ptr<cv::Mat>(
        frame_ptr,
        [this, idx](cv::Mat*) {
            this->release(idx);  // returns buffer to pool
        }
    );
}
```

**Key design property**: Aliasing `shared_ptr` — the control block owns the deleter call, NOT the memory (the pool owns buffers permanently). When refcount hits zero: deleter runs → `release(idx)` → buffer reusable. No heap alloc/free in the hot path.

`acquire()` (raw size_t) is kept for backward compatibility — existing usages (if any) continue to work.

### 2. ThreadSafeQueue — template change

| Where | Current | New |
|-------|---------|-----|
| `inference_orchestrator.hpp` | `ThreadSafeQueue<cv::Mat>&` | `ThreadSafeQueue<std::shared_ptr<cv::Mat>>&` |
| `inference_orchestrator.cpp` | `cv::Mat frame = queue_.pop();` | `auto frame_ptr = queue_.pop();` |
| `test_vision_pipeline.cpp` | `queue.push(cv::Mat::zeros(...))` | `queue.push(std::make_shared<cv::Mat>(...))` |
| `main.cpp` | `ThreadSafeQueue<cv::Mat> queue(8);` | `ThreadSafeQueue<std::shared_ptr<cv::Mat>> queue(8);` |

**Consumer loop change** (`inference_orchestrator.cpp`):
```cpp
auto frame_ptr = queue_.pop();
if (!frame_ptr) continue;       // shutdown → empty shared_ptr
PrimitiveBatch primitives = engine_.infer(*frame_ptr);
// shared_ptr goes out of scope → deleter calls pool_.release(index)
```

No explicit `pool_.release()` call needed — the shared_ptr's custom deleter handles it automatically.

### 3. IVideoProducer interface (NEW)

```cpp
// include/video_producer.hpp
#pragma once

namespace gema { namespace vision {

class IVideoProducer {
public:
    virtual ~IVideoProducer() = default;
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual bool is_running() const = 0;
    virtual void set_frame_rate(double fps) = 0;
};

}}  // namespace gema::vision
```

No pool or queue in the abstract interface — both are injected via each implementation's constructor. Keeps the contract minimal.

### 4. MockVideoProducer (NEW — header-only)

```cpp
// include/mock_video_producer.hpp
class MockVideoProducer final : public IVideoProducer {
public:
    MockVideoProducer(FramePool& pool,
                      ThreadSafeQueue<std::shared_ptr<cv::Mat>>& queue,
                      double fps = 1.5);
    ~MockVideoProducer();

    // IVideoProducer
    void start() override;
    void stop() override;
    bool is_running() const noexcept override;
    void set_frame_rate(double fps) override;

    // Test helpers
    uint64_t frames_produced() const noexcept;
    enum class MockPattern {
        SOLID_COLOR, GRADIENT, NOISE, CHECKERBOARD, TEST_CARD
    };
    void set_pattern(MockPattern pattern);

private:
    void producer_loop();
    static void paint_pattern(cv::Mat& frame, uint64_t frame_index);
    static void sleep_interruptible(std::chrono::microseconds total,
                                    const std::atomic<bool>& running);

    FramePool& pool_;
    ThreadSafeQueue<std::shared_ptr<cv::Mat>>& queue_;
    std::unique_ptr<std::thread> thread_;
    std::atomic<bool> running_{false};
    std::atomic<uint64_t> frames_produced_{0};
    std::chrono::microseconds interval_;
    std::mutex interval_mutex_;
    MockPattern pattern_{MockPattern::SOLID_COLOR};
};
```

**Producer loop**:
```
while (running_):
    auto frame = pool_.acquire_shared()   // shared_ptr with deleter
    paint_pattern(*frame, frame_counter)
    queue_.push(frame)                     // move shared_ptr into queue
    frames_produced_++
    sleep_interruptible(interval_, running_)
```

**`sleep_interruptible()`**: Splits sleep into 100ms chunks, checking `running_` between chunks — enables sub-period shutdown response.

**5 patterns** (cycled via `frame_index % 5`):

| Pattern | Visual | Exercises |
|---------|--------|-----------|
| SOLID_COLOR | R→G→B→W→... cycling | Frame delivery, basic pipeline |
| GRADIENT | Horizontal + vertical sweep | Pixel data integrity through queue |
| NOISE | Uniform random | High-frequency content handling |
| CHECKERBOARD | 32×32 alternating squares | Bbox detection across full frame |
| TEST_CARD | 3 known bboxes at fixed positions | End-to-end sanity (defect, OCR, color) |

### 5. RgaVideoProducer (NEW — placeholder)

```cpp
// include/rga_video_producer.hpp
class RgaVideoProducer final : public IVideoProducer {
public:
    RgaVideoProducer(FramePool& pool,
                     ThreadSafeQueue<std::shared_ptr<cv::Mat>>& queue,
                     const std::string& camera_device = "/dev/video0");
    ~RgaVideoProducer();
    // ... IVideoProducer interface ...

private:
    void producer_loop();
    FramePool& pool_;
    ThreadSafeQueue<std::shared_ptr<cv::Mat>>& queue_;
    std::unique_ptr<std::thread> thread_;
    std::atomic<bool> running_{false};
};
```

Guarded by `#ifdef __arm__` in the implementation file — compiling for x86 silently skips it. Production build for RV1106 will define `__arm__` and it compiles; CI on x86 skips it without error.

### 6. InferenceOrchestrator — signature change

```cpp
// Constructor — BEFORE
InferenceOrchestrator(InferenceEngine& engine,
    ThreadSafeQueue<cv::Mat>& frame_queue, ...);

// Constructor — AFTER
InferenceOrchestrator(InferenceEngine& engine,
    ThreadSafeQueue<std::shared_ptr<cv::Mat>>& frame_queue, ...);
```

The `consumer_loop()` pops `shared_ptr<cv::Mat>`, dereferences it for `engine_.infer()`, and lets the shared_ptr go out of scope (triggering pool release via custom deleter). No `FramePool&` needs to be injected into the orchestrator — the deleter is baked into the shared_ptr at acquisition time.

### 7. main.cpp — wiring + signal handler fix

**Signal handler** (BEFORE):
```cpp
extern "C" void signal_handler(int sig) {
    write(...);
    g_shutdown.store(true);
    g_watchdog->stop();     // NOT async-signal-safe
    std::_Exit(128 + sig);  // kills threads mid-operation
}
```

**Signal handler** (AFTER):
```cpp
extern "C" void signal_handler(int sig) {
    const char msg[] = "[gema-vision] Signal received\n";
    ::write(STDERR_FILENO, msg, sizeof(msg) - 1);
    g_shutdown.store(true, std::memory_order_release);
    // NO watchdog.stop() — not safe from signal context
    // NO _Exit() — let main() drain cleanly
}
```

**Main shutdown wiring**:
```cpp
// ---- Create components --------------------------------------------
FramePool pool(640, 480, CV_8UC3);
ThreadSafeQueue<std::shared_ptr<cv::Mat>> queue(8);

#if defined(USE_MOCK_PRODUCER)
    MockVideoProducer producer(pool, queue, 1.5);
#else
    RgaVideoProducer producer(pool, queue, "/dev/video0");
#endif

// InferenceOrchestrator with existing dependencies
InferenceOrchestrator orchestrator(engine, queue, mqtt_client,
                                    calibrator_, dispatcher_);

// ---- Start order --------------------------------------------------
g_watchdog->start();
producer.start();
orchestrator.start();

// ---- Wait for shutdown --------------------------------------------
while (!g_shutdown.load(std::memory_order_acquire)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

// ---- Shutdown order (REVERSE) -------------------------------------
orchestrator.stop();   // 1. drain queue, join consumer thread
producer.stop();       // 2. join producer thread
g_watchdog->stop();    // 3. disarm WDT
return 0;
```

---

## Thread Model

| Thread | Role | Created by | Blocking point |
|--------|------|------------|----------------|
| Main | Waits on `g_shutdown`, orchestrates lifecycle | `main()` | `sleep(100ms)` loop |
| Producer | Paints synthetic frames | `producer.start()` | `sleep_interruptible()` |
| Consumer | Runs inference, publishes results | `orchestrator.start()` | `queue_.pop()` |
| Watchdog | Pets HW WDT | `g_watchdog->start()` | `sleep(interval)` |

**Synchronization**:
- Producer ↔ Consumer: `ThreadSafeQueue` (mutex + condvar)
- Signal → Main: `std::atomic<bool> g_shutdown` (release/acquire)
- Producer `interval_`: `std::mutex interval_mutex_` (set_frame_rate is cross-thread)

**Why no deadlocks**: The queue is bounded at 8 with drop-oldest — producer never blocks on push. Consumer pops with a timeout-resilient condvar. Shutdown is strictly linear (no cycles in the stop dependency graph).

---

## Data Flow

```
Pool acquire_shared()
  │
  ├─► Frame[0] ── painted ──► Queue push()
  ├─► Frame[1] ── painted ──► Queue push()
  ├─► Frame[2] ── painted ──► Queue push()
  │
  │   Consumer pops:
  │     shared_ptr<Mat> goes to engine.infer()
  │     shared_ptr goes out of scope
  │     deleter calls pool_.release(index)  ←─ buffer reusable
  │
  ▼
Producer acquires Frame[3] (oldest released by consumer)
```

**Zero-copy invariant**: The `cv::Mat` pixel data lives in the pool buffer from acquisition to deletion. The shared_ptr owns only the reference count and the deleter. No `clone()`, no `malloc()` in the hot path.

---

## File Changes

| File | Action | Description |
|------|--------|-------------|
| `include/video_producer.hpp` | **Create** | `IVideoProducer` abstract interface |
| `include/mock_video_producer.hpp` | **Create** | `MockVideoProducer` — header-only, 5 synthetic patterns |
| `include/rga_video_producer.hpp` | **Create** | `RgaVideoProducer` placeholder (`#ifdef __arm__`) |
| `include/frame_pool.hpp` | **Modify** | Add `acquire_shared()` method returning `shared_ptr<cv::Mat>` |
| `include/inference_orchestrator.hpp` | **Modify** | Change `ThreadSafeQueue<cv::Mat>&` → `ThreadSafeQueue<std::shared_ptr<cv::Mat>>&` |
| `src/inference_orchestrator.cpp` | **Modify** | Adapt `consumer_loop()` for shared_ptr pop/deref |
| `src/main.cpp` | **Modify** | Wire producer lifecycle, fix signal handler (no `_Exit()`, no `watchdog.stop()`) |
| `CMakeLists.txt` | **Modify** | Add test_vision_producer target, update test_vision_pipeline deps |
| `test/test_vision_pipeline.cpp` | **Modify** | Update queue template parameter, use `make_shared` for frame pushes |
| `test/test_video_producer.cpp` | **Create** | SIL tests for MockVideoProducer |

---

## Testing Strategy

| Layer | What to Test | Approach |
|-------|-------------|----------|
| Unit | `FramePool::acquire_shared()` | Verifies custom deleter calls `release()` on refcount=0 |
| Unit | `MockVideoProducer` pattern generation | Generate each pattern, verify pixel content |
| Integration | Producer → Queue → Consumer | 3-frame run: push → pop → process → release, verify pool buffers recycled |
| Integration | Signal handler correctness | Verify no `_Exit()` call, no `watchdog.stop()` in handler |
| Integration | Graceful shutdown sequence | Send SIGTERM, verify orchestrator→producer→watchdog stop order |
| Edge | All buffers in flight | Acquire 5 frames from 4-buffer pool — verify spin/yield behaviour |
| Edge | Queue overflow | Push 10 frames into max-8 queue — verify oldest 2 dropped |

---

## Risk Assessment

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| `shared_ptr` ctrl block alloc failure (32 bytes) | Near zero | Crash | Same risk as any `shared_ptr` usage in the project |
| Consumer slower than producer | Low (26x margin) | Queue grows | Drop-oldest at 8 frames bounds memory |
| Signal handler reentrancy | Very low | UB | Signal is blocked during handler (default `sigaction` behaviour) |
| Pool buffer use-after-free | Low | Data corruption | Shared_ptr custom deleter guarantees release only after last reference drops |
| `acquire_shared()` blocks forever | Low (4 buffers) | Hung producer | All 4 buffers in flight means consumer is processing them — will release |
| Mock producer in production build | Medium | Wrong binary | CMake `USE_MOCK_PRODUCER` preprocessor guard; not set in release |

---

## Open Questions

- [ ] Confirm the 5 mock pattern set is sufficient for SIL validation
- [ ] Confirm FramePool width/height/type (640×480, CV_8UC3 as default)
- [ ] Decide if `acquire()` (raw index) should be removed or kept for backward compat
