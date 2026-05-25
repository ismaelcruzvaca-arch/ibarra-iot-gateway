# Exploration: VideoProducer Abstraction + Mock

## Current State

The vision pipeline currently has:

1. **Consumer side only**: `InferenceOrchestrator` with a `consumer_loop()` that blocks on `ThreadSafeQueue<cv::Mat>::pop()`, runs inference, and publishes results via MQTT. It's fully implemented with SIL tests.
2. **No producer thread**: The existing `gema_vision/src/main.cpp` starts a hardware watchdog and then spins in a `while (!g_shutdown)` sleep loop — no frame capture occurs. The older `vision_edge/src/main.cpp` is a simple synchronous loop that doesn't use the pipeline at all.
3. **FramePool**: A 4-buffer ring-buffer (`gema::vision::FramePool`) with `acquire()`/`release()` by index. Pre-allocated `cv::Mat` buffers at construction. `acquire()` uses a spin-lock (CAS on `available_[]`) and yields when all buffers are in flight. At 1.5 Hz with 4 buffers, this is safe.
4. **ThreadSafeQueue**: Bounded queue (max 8, drop-oldest) with `push()`, `pop()` (blocking), `try_pop()`, `shutdown()`. Uses `std::mutex` + `std::condition_variable`.
5. **FlashTrigger interface**: Abstract interface for hardware GPIO trigger — `wait_for_flash()`. This is a DIFFERENT concern from the VideoProducer. `MockFlashTrigger` sleeps for 666ms to simulate 90 strokes/min.
6. **Signal handling**: `gema_vision/src/main.cpp` has an async-signal-safe handler that sets `g_shutdown = true`, calls `g_watchdog->stop()`, then calls `std::_Exit(128 + sig)`. The `_Exit()` call **terminates immediately** without running destructors — this kills any in-flight producer/consumer threads mid-operation.

### Key insight: Two namespaces

| Namespace | Location | Contains |
|-----------|----------|----------|
| `gema::vision` | `edge_ops/gema_vision/` | Main pipeline: FramePool, ThreadSafeQueue, InferenceOrchestrator, InferenceEngine, FlashTrigger, Watchdog (HW), all mocks |
| `vision::` | `vision_edge/` | Older daemon: simple watchdog + MQTT + engine |
| `vision::watchdog` | `vision_edge/` | Software watchdog (heartbeat-based) |

The new VideoProducer belongs in **`gema::vision`** — that's where the pipeline lives.

## Affected Areas

| File | Status | Why |
|------|--------|-----|
| `edge_ops/gema_vision/include/video_producer.hpp` | **NEW** | `IVideoProducer` abstract interface |
| `edge_ops/gema_vision/include/mock_video_producer.hpp` | **NEW** | `MockVideoProducer` — header-only, inline, synthetic frame generation |
| `edge_ops/gema_vision/include/rga_video_producer.hpp` | **NEW** | `RgaVideoProducer` — placeholder with static_assert |
| `edge_ops/gema_vision/include/frame_pool.hpp` | Unchanged | Already has `acquire()`/`release()` and `operator[]` — no changes needed |
| `edge_ops/gema_vision/src/main.cpp` | **MODIFY** | Wire producer + orchestrator lifecycle, fix `_Exit()` shutdown |
| `edge_ops/gema_vision/CMakeLists.txt` | **MODIFY** | Add new source files for library and test |
| `edge_ops/gema_vision/test/test_video_producer.cpp` | **NEW** | SIL tests for MockVideoProducer |
| `edge_ops/gema_vision/include/inference_engine.hpp` | Unchanged | Not affected |
| `edge_ops/gema_vision/include/flash_trigger.hpp` | Unchanged | Separate concern (HW trigger vs synthetic frame gen) |

## Architecture Design

### 1. IVideoProducer Interface (`video_producer.hpp`)

```cpp
namespace gema {
namespace vision {

class IVideoProducer {
public:
    virtual ~IVideoProducer() = default;

    /// Start the producer thread. Call returns immediately.
    virtual void start() = 0;

    /// Gracefully stop the producer thread. Blocks until joined.
    virtual void stop() = 0;

    /// True if the producer thread is running.
    virtual bool is_running() const noexcept = 0;

    /// Set the frame rate in Hz. Used by MockVideoProducer to control pace.
    virtual void set_frame_rate(double fps) = 0;
};

}  // namespace vision
}  // namespace gema
```

**Design decisions:**
- Pure virtual interface — no `FramePool` or `ThreadSafeQueue` in the interface itself. Instead, both are injected via each implementation's constructor. This avoids coupling the abstract contract to the concrete pool/queue types while still enforcing the pattern.
- `set_frame_rate()` is on the interface because it's a fundamental control parameter that any producer (mock or real) should expose. Even the RGA camera will benefit from being able to set the desired FPS.
- Non-copiable, non-movable is enforced at the implementation level (each impl deletes copy/move).

### 2. MockVideoProducer (`mock_video_producer.hpp`)

**Header-only, inline** — follows the same pattern as `MockEngine` and `MockFlashTrigger`.

```cpp
class MockVideoProducer final : public IVideoProducer {
public:
    MockVideoProducer(FramePool& pool,
                      ThreadSafeQueue<cv::Mat>& queue,
                      double fps = 1.5);
    ~MockVideoProducer();  // calls stop()

    // Non-copiable, non-movable
    MockVideoProducer(const MockVideoProducer&) = delete;
    MockVideoProducer& operator=(const MockVideoProducer&) = delete;
    MockVideoProducer(MockVideoProducer&&) = delete;
    MockVideoProducer& operator=(MockVideoProducer&&) = delete;

    // IVideoProducer
    void start() override;
    void stop() override;
    bool is_running() const noexcept override;
    void set_frame_rate(double fps) override;

    // Test helpers
    uint64_t frames_produced() const noexcept;
    void set_pattern(MockPattern pattern);

private:
    void producer_loop();
    void paint_pattern(cv::Mat& frame, uint64_t frame_index);

    FramePool& pool_;
    ThreadSafeQueue<cv::Mat>& queue_;
    std::unique_ptr<std::thread> thread_;
    std::atomic<bool> running_{false};
    std::atomic<uint64_t> frames_produced_{0};
    std::chrono::microseconds interval_;  // = 1/fps in microseconds
    std::mutex interval_mutex_;  // protects interval_ (set from another thread)
    MockPattern pattern_{MockPattern::SOLID_COLOR};
};
```

**Thread structure (`producer_loop`):**

```
while (running_):
    idx = pool_.acquire()          // get next free buffer
    cv::Mat& frame = pool_[idx]    // direct reference, zero-copy
    
    // --- RAII guard: release on scope exit ---
    // (implemented inside producer_loop as a local FrameGuard)
    
    paint_pattern(frame, frame_counter)
    queue_.push(frame)             // shallow copy of cv::Mat header
    frames_produced_++
    
    sleep_for(interval_)           // e.g. ~666ms for 1.5 Hz
    // check running_ every 100ms in slices to enable fast shutdown
```

**sleep strategy:** Split the interval into 100ms chunks to allow responsive shutdown. For 666ms at 1.5 Hz, this means 6-7 iterations of sleep(100ms) with `running_` check in between. This is implemented as a helper:

```cpp
static void sleep_interruptible(std::chrono::microseconds total,
                                const std::atomic<bool>& running) {
    constexpr auto CHUNK = std::chrono::milliseconds(100);
    auto remaining = total;
    while (remaining > CHUNK && running.load()) {
        std::this_thread::sleep_for(CHUNK);
        remaining -= CHUNK;
    }
    if (running.load()) {
        std::this_thread::sleep_for(remaining);
    }
}
```

**Pattern generation (`paint_pattern`):**

| Pattern | Description | Visual | Pipeline Exercise |
|---------|-------------|--------|-------------------|
| `SOLID_COLOR` | Solid color cycling R→G→B→W→... based on frame_index | Simple colored frame | Verifies frame arrives at consumer at all |
| `GRADIENT` | Horizontal + vertical gradient | Smooth color transitions | Tests that pixel data integrity is preserved through the queue |
| `NOISE` | Uniform random noise | Static-like | Tests that inference engine handles high-frequency content |
| `CHECKERBOARD` | Alternating black/white 32x32 squares | Calibration pattern | Tests bbox detection across the full frame |
| `TEST_CARD` | 3 known bboxes (defect, OCR zone, color zone) at fixed positions | Primitive-like | Full end-to-end sanity check |

Each frame gets the pattern painted based on `frame_index % pattern_count`, so at 1.5 Hz the full cycle repeats every ~3.3 seconds (5 patterns).

### 3. FrameGuard RAII Wrapper

A minimal struct to ensure the acquired buffer is returned to the pool even if the producer thread is stopped or an exception is thrown:

```cpp
struct FrameGuard {
    FramePool& pool;
    size_t     index;
    bool       released = false;

    FrameGuard(FramePool& p, size_t idx) noexcept : pool(p), index(idx) {}

    ~FrameGuard() noexcept {
        if (!released) {
            pool.release(index);
        }
    }

    // Non-copiable, non-movable
    FrameGuard(const FrameGuard&) = delete;
    FrameGuard& operator=(const FrameGuard&) = delete;
    FrameGuard(FrameGuard&&) = delete;
    FrameGuard& operator=(FrameGuard&&) = delete;

    void dismiss() noexcept { released = true; }
};
```

**Usage in producer_loop:**
```cpp
size_t idx = pool_.acquire();
FrameGuard guard(pool_, idx);
cv::Mat& frame = pool_[idx];

paint_pattern(frame, frames_produced_.load());

queue_.push(frame);  // shallow copy into queue
guard.dismiss();     // queue now owns the "logical ownership" — pool release happens after consumer finishes
```

**Important nuance:** `dismiss()` is called AFTER `queue_.push()` because the queue holds only a shallow `cv::Mat` header copy (the pixel data lives in the pool buffer). But the pool needs the buffer back when the CONSUMER finishes, not when the producer finishes. This means **the pool release must happen in the consumer** (or the queue must carry the index).

**Design fork here:**

| Approach | How it works | Pros | Cons |
|----------|--------------|------|------|
| **A: Push index, consumer releases** | Push `size_t` index to queue; consumer calls `pool.release(idx)` after processing | True zero-copy, pool lifecycle matches actual usage | Queue template changes from `cv::Mat` to `size_t`; orchestrator must know about pool |
| **B: Push cv::Mat shallow copy, never release** | Push `cv::Mat` header (shared pixel data); never explicitly release pool buffers (pool becomes a recycling allocator with unused slots) | No API changes to queue or orchestrator | Pool buffers are never returned; after 4 frames the producer blocks forever |
| **C: Push FrameWrapper (index + Mat reference)** | Small struct carrying both the index and the frame reference; consumer extracts index and calls release after processing | Clean ownership semantics | Requires orchestrator changes to extract and release |

**Recommendation: Approach A** — change the queue template parameter OR use a wrapper struct. The consumer already has `FramePool` available (it can be injected). This is the clearest ownership model. The `InferenceOrchestrator` signature changes slightly:

```cpp
// Instead of:
ThreadSafeQueue<cv::Mat>& frame_queue_;

// Use a small wrapper:
struct FrameTicket {
    cv::Mat frame;    // shallow copy (same pixel data as pool buffer)
    size_t  pool_index;
};

ThreadSafeQueue<FrameTicket>& frame_queue_;
```

But this adds coupling. A cleaner approach is to have the queue carry `cv::Mat` but inject the pool into the orchestrator so it can release:

Actually, the simplest approach matching the existing code: **keep pushing `cv::Mat`** but make the pool release happen in the **producer only**. This works because:

1. Producer acquires pool buffer, paints into it, pushes a `cv::Mat` **clone** (deep copy) to the queue.
2. Producer releases the pool buffer immediately.

But this defeats the zero-alloc purpose — `clone()` allocates. Let me reconsider.

**Correct design:** The pool is a ring-buffer. The producer writes into slot N, pushes a signal to the consumer. The consumer reads slot N, processes it, releases slot N. This is the classic ring-buffer pattern.

So the queue should carry **indices**, not `cv::Mat`. The FrameTicket approach:

```cpp
struct FrameTicket {
    size_t  pool_index;
    // cv::Mat is accessed via pool_[pool_index] by the consumer
};
```

The `InferenceOrchestrator` would need a `FramePool&` injected. This is a **breaking change** to the existing orchestrator interface.

**Alternative (pragmatic):** Keep the queue as `ThreadSafeQueue<cv::Mat>` and do a shallow copy. The `cv::Mat` copy constructor is O(1) — it only copies the header (rows, cols, type, data pointer, step). The pixel data is shared. The pool buffer is effectively "owned" by the queue item until the consumer finishes with it and the `cv::Mat` destructor runs... but the destructor does NOT release the pool buffer back.

So we need the consumer to release explicitly. This means the consumer needs the pool reference AND the index. Since `cv::Mat` doesn't carry an index, we need to associate them.

**Final recommendation: Use a `FrameTicket` struct and keep the queue template as `ThreadSafeQueue<FrameTicket>`.** The FrameTicket is trivially copyable (just a `cv::Mat` + `size_t`). The producer:

```cpp
size_t idx = pool_.acquire();
FrameGuard guard(pool_, idx);  // releases on exception
cv::Mat& frame = pool_[idx];
paint_pattern(frame, counter);
queue_.push(FrameTicket{frame, idx});  // shallow copy of cv::Mat header
guard.dismiss();  // consumer is responsible for release now
```

And the consumer:
```cpp
FrameTicket ticket = frame_queue_.pop();
// ... process ticket.frame ...
pool_.release(ticket.pool_index);
```

This requires modifying `InferenceOrchestrator` to:
1. Accept a `FramePool&` in its constructor
2. Call `pool_.release(ticket.pool_index)` after processing
3. Change `ThreadSafeQueue<cv::Mat>&` to `ThreadSafeQueue<FrameTicket>&`

This is a non-trivial refactor of an already-implemented component. Worth flagging for the proposal phase.

### 4. RgaVideoProducer Placeholder

```cpp
class RgaVideoProducer final : public IVideoProducer {
public:
    RgaVideoProducer(FramePool& pool,
                     ThreadSafeQueue<FrameTicket>& queue,
                     const std::string& camera_device = "/dev/video0");
    ~RgaVideoProducer();

    // IVideoProducer
    void start() override;
    void stop() override;
    bool is_running() const noexcept override;
    void set_frame_rate(double fps) override;

private:
    void producer_loop();
    // RGA-specific: drm_flip(), rga_import(), etc.
};
```

Placeholder implementation:
```cpp
void start() override {
    static_assert(sizeof(void*) == 0,
        "RgaVideoProducer not yet implemented — hardware pending");
}
```

This means **attempting to use RgaVideoProducer before hardware arrives causes a compile error**, preventing accidental deployment.

### 5. main.cpp Wiring Changes

**Current** (`gema_vision/src/main.cpp`):
```cpp
while (!g_shutdown.load(std::memory_order_acquire)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}
return 0;
```

**Wired version**:
```cpp
// ---- Frame Pool (pre-allocate 4 buffers at 640x480) ---------------
FramePool pool(640, 480, CV_8UC3);
ThreadSafeQueue<FrameTicket> queue(8);

// ---- Producer ------------------------------------------------------
#if defined(USE_MOCK_PRODUCER)
    MockVideoProducer producer(pool, queue, 1.5);
#else
    // When hardware arrives:
    RgaVideoProducer producer(pool, queue, "/dev/video0");
#endif

// ---- Orchestrator (consumer) ---------------------------------------
// NOTE: InferenceEngine and MqttClient TBD — for now:
// Use a simple engine that just releases tickets
MockEngine engine(20);
MockMqttClient mqtt;
// (calibrator_ and dispatcher_ from existing code)

InferenceOrchestrator orchestrator(engine, queue, mqtt,
                                   calibrator_, dispatcher_, &pool);
// pool injection for consumer-side release

// ---- Start order ---------------------------------------------------
g_watchdog->start();
producer.start();
orchestrator.start();

// ---- Wait for shutdown ---------------------------------------------
while (!g_shutdown.load(std::memory_order_acquire)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

// ---- Shutdown order (REVERSE of start) -----------------------------
orchestrator.stop();   // 1. drain queue, stop consumer
producer.stop();       // 2. stop producer (won't acquire after stop)
g_watchdog->stop();    // 3. disarm watchdog
```

**Critical fix: Remove `_Exit()` from signal handler.**

The current handler:
```cpp
extern "C" void signal_handler(int sig) {
    // ... write message ...
    g_shutdown.store(true, std::memory_order_release);
    if (g_watchdog) g_watchdog->stop();
    std::_Exit(128 + sig);   // ← THIS KILLS THREADS MID-FRAME
}
```

Replace with:
```cpp
extern "C" void signal_handler(int sig) {
    const char msg[] = "[gema-vision] Signal received — shutting down\n";
    if (::write(STDERR_FILENO, msg, sizeof(msg) - 1) < 0) { /* ignore */ }
    g_shutdown.store(true, std::memory_order_release);
    // DO NOT call watchdog->stop() here — it's not async-signal-safe
    // DO NOT call _Exit() — let main() drain threads cleanly
}
```

Move watchdog stop to main():
```cpp
// After the while(!g_shutdown) loop:
orchestrator.stop();
producer.stop();
if (g_watchdog) g_watchdog->stop();  // disarm before exit
```

**Why this is safe:** The signal handler still sets `g_shutdown` atomically, which causes the main loop to exit. The main thread then drains orchestrator → producer → watchdog in reverse order. The signal handler is still async-signal-safe because it only calls `write()` and an atomic store.

**But what about the watchdog keepalive thread?** If the main thread is blocked on orchestrator.stop() (waiting for consumer to finish), the watchdog keepalive thread is still running and pinging the WDT. This means the watchdog will NOT timeout during clean shutdown — which is correct. Only if the process hangs during shutdown (deadlock) would the WDT fire, which is the desired safety behaviour.

### 6. Shutdown Sequence (detailed)

```
SIGTERM/SIGINT
    │
    ▼
signal_handler()
    ├── write() to stderr (async-signal-safe)
    └── g_shutdown.store(true, release)
    [returns — no _Exit()]
    │
    ▼
main() exits while(!g_shutdown) loop
    │
    ├── (1) orchestrator.stop()
    │       ├── running_.store(false)
    │       ├── frame_queue_.shutdown()   // wakes consumer
    │       ├── consumer_loop sees empty+shutdown → breaks
    │       └── thread_.join()
    │
    ├── (2) producer.stop()
    │       ├── running_.store(false)
    │       ├── producer_loop checks running_ after sleep → exits
    │       └── thread_.join()
    │       └── [FrameGuard destructor on stack releases pool buffer]
    │
    ├── (3) g_watchdog->stop()
    │       ├── sends magic close 'V' to /dev/watchdog
    │       ├── closes fd
    │       └── thread_.join()
    │
    └── return 0
```

**Key properties:**
- **No thread is killed mid-operation** — each stop() joins cleanly
- **FramePool buffers are always released** — either via FrameGuard (if producer was painting) or via orchestrator release (after processing)
- **Watchdog is disarmed** before exit — prevents unwanted SoC reset
- **Deadlock-safe**: if any join hangs, the HW WDT will fire after its timeout (~15s) and reset the SoC

### 7. Thread Timing Analysis

At 1.5 Hz (666ms period):

| Event | Duration | Cumulative |
|-------|----------|------------|
| pool.acquire() | <1 µs (spin, 4 buffers) | 0 µs |
| paint_pattern() | ~0.5-2 ms (Mat operations) | ~2 ms |
| queue.push(FrameTicket) | ~5 µs (mutex + condvar) | ~2 ms |
| sleep (remainder) | ~664 ms | 666 ms |
| **Total frame time** | **~666 ms** | 666 ms |

Consumer side:
| Event | Duration |
|-------|----------|
| queue.pop() (blocking) | ~0-666 ms (wait for next frame) |
| engine.infer() | ~20 ms (MockEngine latency) |
| post-process + MQTT | ~1-5 ms |
| pool.release(idx) | <1 µs |
| **Total consumer time** | **~25 ms per frame** |

Consumer is 26x faster than producer — no queue buildup at steady state. Maximum queue depth: 1 frame (briefly during handover).

## Approaches Considered

### A. Push pool indices, inject pool into consumer (RECOMMENDED)
**How:** Queue carries `FrameTicket{frame, index}`. Orchestrator receives pool reference and releases after processing.
- **Pros:** True zero-copy, clear ownership, maximum efficiency
- **Cons:** Requires modifying InferenceOrchestrator interface, breaking change to existing code
- **Effort:** Medium (orchestrator refactor + FrameTicket type)

### B. Push cv::Mat deep copy (clone), release pool immediately
**How:** Producer clones the pool buffer, pushes the clone, releases the pool slot immediately.
- **Pros:** No changes to queue or orchestrator, simple
- **Cons:** 900 KB malloc+free per frame at 1.5 Hz = **exactly what FramePool was designed to avoid**
- **Effort:** Low
- **Verdict:** REJECTED — defeats the purpose of FramePool

### C. Push cv::Mat shallow copy, never release pool
**How:** Producer acquires pool buffer, paints, pushes shallow copy. Pool is never released. After 4 frames, producer blocks forever.
- **Pros:** Zero changes
- **Cons:** Pool exhausts after 4 frames at 1.5 Hz (~2.7s)
- **Effort:** None
- **Verdict:** REJECTED — doesn't work

### D. Keep cv::Mat queue, inject pool into orchestrator for release
**How:** Keep `ThreadSafeQueue<cv::Mat>`. Orchestrator receives `FramePool&` and a separate index queue (`ThreadSafeQueue<size_t>`). Producer pushes index to second queue after frame. Orchestrator pops both.
- **Pros:** No change to existing queue template
- **Cons:** Two queues to synchronize, complex, error-prone
- **Effort:** Medium
- **Verdict:** REJECTED — overly complex

## Risks and Edge Cases

1. **Orchestrator interface breakage**: Changing `ThreadSafeQueue<cv::Mat>&` to `ThreadSafeQueue<FrameTicket>&` in `InferenceOrchestrator` is a breaking change. All existing tests and mock usages must be updated. The `FrameTicket` struct must be defined in a shared header.

2. **cv::Mat shallow copy + pool release race**: If the consumer reads the frame but hasn't finished using the pixel data when the pool buffer is released (and the producer overwrites it), we get data corruption. Solution: the consumer MUST call `pool.release()` AFTER all processing is done. The RAII pattern inside `consumer_loop()` ensures this:

   ```cpp
   FrameTicket ticket = frame_queue_.pop();
   // process ticket.frame
   // ... use pixel data ...
   pool_.release(ticket.pool_index);  // only now is the buffer reusable
   ```

3. **Signal handler reentrancy**: The `write()` call in the signal handler is technically async-signal-safe, but if `SIGTERM` arrives while another `SIGTERM` is being handled, undefined behaviour can occur. Use `SA_NODEFER` / `sigaction()` with `sa_flags = 0` (default blocks the signal during handler). The current `std::signal()` call doesn't provide this guarantee. Consider switching to `sigaction()` in the final implementation.

4. **FrameGuard on exception**: If `paint_pattern()` throws (e.g. bad allocation), the `FrameGuard` destructor calls `pool_.release()`, returning the buffer. BUT `ThreadSafeQueue::push()` is not called, so the frame is lost. At 1.5 Hz this is acceptable — the consumer will block waiting for the next frame. The watchdog will detect if the hang exceeds 15s.

5. **Interval underflow on fast shutdown**: If `set_frame_rate(1000)` is called (extreme but possible), the interval becomes 1µs. The `sleep_interruptible()` function will handle this correctly by sleeping for `remaining` even if it's sub-100ms. No underflow.

6. **MockVideoProducer in production build**: The mock producer must NOT be linked into the production daemon binary. Use CMake `target_sources` with `$<CONFIG:Debug>` or an explicit `BUILD_MOCK_PRODUCER` option. The `USE_MOCK_PRODUCER` preprocessor define in `main.cpp` gates this.

7. **Watchdog stop in signal handler**: Currently `g_watchdog->stop()` is called inside the signal handler. `stop()` calls `thread_.join()` which is NOT async-signal-safe. If the signal arrives while the watchdog thread is holding a lock, `join()` could deadlock. **Fix**: remove the watchdog stop from the signal handler. Let main() handle it after the shutdown flag is observed.

## Recommendation

Proceed with **Approach A** (FrameTicket + pool injection into orchestrator). This is the architecturally correct solution for a zero-alloc pipeline. The breaking change to `InferenceOrchestrator` is acceptable because:

1. The existing tests can be quickly updated (change `ThreadSafeQueue<cv::Mat>` to `ThreadSafeQueue<FrameTicket>`, add `FramePool` injection with `nullptr` for tests that don't need pool release).
2. The `FrameTicket` struct is tiny and trivially constructible.
3. This is the LAST time we change the queue type — once sealed, the contract is final.

The `main.cpp` changes (remove `_Exit()`, wire lifecycle, fix signal handler) are independent and should be done regardless of the approach chosen.

## Ready for Proposal

**Yes** — the design is fully explored. Key decision points to confirm with the user:

1. **FrameTicket approach**: Accept the breaking change to `InferenceOrchestrator`?
2. **Signal handler fix**: Remove `_Exit()` and `watchdog.stop()` from handler?
3. **Mock pattern set**: Are the 5 proposed patterns sufficient?
4. **Namespace**: Confirm the producer belongs in `gema::vision` (not a new namespace)?
