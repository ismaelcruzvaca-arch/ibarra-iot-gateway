# Delta Spec: VideoProducer Abstraction + Mock

## Rationale

The vision pipeline has a consumer (`InferenceOrchestrator`) but no producer — `main.cpp` spins in an empty sleep loop. Adding a `IVideoProducer` abstraction enables SIL testing of the full pipeline (producer → queue → consumer → MQTT) before RGA camera hardware arrives. A `MockVideoProducer` generates synthetic frames at 1.5 Hz using 5 patterns, exercising the frame pool, zero-copy queue handover, and inference path without any camera.

This change also fixes two safety bugs in `main.cpp`: the signal handler calls `watchdog.stop()` (not async-signal-safe) and `std::_Exit()` (kills threads mid-frame).

---

## ADDED Requirements

### Requirement: IVideoProducer Abstract Interface

A video producer SHALL implement the `gema::vision::IVideoProducer` interface.

- **R-ADD-1:** `void start()` — SHALL launch the producer's internal thread and return immediately.
- **R-ADD-2:** `void stop()` — SHALL signal the thread to exit and block until joined.
- **R-ADD-3:** `bool is_running() const noexcept` — SHALL return `true` after `start()` returns, `false` after `stop()` returns.
- **R-ADD-4:** `void set_frame_rate(double fps)` — SHALL set the target frame rate. The producer SHOULD respect this value in its sleep interval.

**File:** `edge_ops/gema_vision/include/video_producer.hpp` (new)

#### Scenario: Start and stop lifecycle

GIVEN a `MockVideoProducer` constructed with a `FramePool` and `ThreadSafeQueue<std::shared_ptr<cv::Mat>>`
WHEN `start()` is called
THEN `is_running()` returns `true` within 100 ms
WHEN `stop()` is called
THEN `is_running()` returns `false`
AND the internal thread has joined

#### Scenario: Double start is idempotent

GIVEN a `MockVideoProducer` in stopped state
WHEN `start()` is called twice
THEN the second call SHALL NOT create a second thread
AND `is_running()` returns `true`

---

### Requirement: RAII Frame Acquisition via `std::shared_ptr<cv::Mat>`

`FramePool::acquire()` SHALL return `std::shared_ptr<cv::Mat>` with a custom deleter that calls `this->release(index)` when the last shared_ptr reference is destroyed.

- **R-ADD-5:** The returned `shared_ptr<cv::Mat>` SHALL point to the pre-allocated buffer at the acquired index.
- **R-ADD-6:** The custom deleter SHALL call `release(index)` exactly once.
- **R-ADD-7:** `FramePool::release(size_t)` SHALL remain unchanged (public, called by the deleter).
- **R-ADD-8:** The existing `acquire()` returning `size_t` MAY be kept or removed; callers MUST migrate to the `shared_ptr` overload.

**File:** `edge_ops/gema_vision/include/frame_pool.hpp` (modified)

#### Scenario: Shared pointer returns buffer on scope exit

GIVEN a `FramePool` with all buffers free
WHEN `acquire()` is called in a block scope
AND the returned `shared_ptr` goes out of scope
THEN the underlying pool buffer becomes available again
AND a subsequent `acquire()` returns the same index

#### Scenario: Shared pointer does not release while in use

GIVEN a `FramePool` with all buffers free
WHEN a `shared_ptr<cv::Mat>` is acquired and copied into a queue
AND the original shared_ptr is destroyed
THEN the pool buffer SHALL NOT be released
AND the queued copy still points to valid pixel data
WHEN the queued copy is destroyed
THEN the pool buffer SHALL be released

---

### Requirement: MockVideoProducer — 5 Synthetic Patterns

`MockVideoProducer` SHALL generate frames using 5 cycling patterns and push them as `std::shared_ptr<cv::Mat>` to the queue.

- **R-ADD-9:** Default frame rate SHALL be 1.5 Hz.
- **R-ADD-10:** The producer SHALL cycle through 5 patterns: `SOLID_COLOR`, `GRADIENT`, `NOISE`, `CHECKERBOARD`, `TEST_CARD`, repeating every 5 frames.
- **R-ADD-11:** `producer_loop()` SHALL use `sleep_interruptible()` in 100 ms chunks to enable responsive shutdown.
- **R-ADD-12:** `MockVideoProducer` SHALL be header-only in `namespace gema::vision`.
- **R-ADD-13:** `frames_produced()` SHALL return the total frame count since `start()`.
- **R-ADD-14:** `set_pattern(MockPattern)` SHALL override the cycling pattern for deterministic testing.

**File:** `edge_ops/gema_vision/include/mock_video_producer.hpp` (new)

#### Scenario: Produces frames at 1.5 Hz

GIVEN a `MockVideoProducer` started with default frame rate
WHEN the producer runs for 2 seconds
THEN `frames_produced()` SHALL be at least 2 and at most 4 (tolerance for timing jitter)

#### Scenario: Stop during sleep exits quickly

GIVEN a `MockVideoProducer` started with 1.5 Hz (666 ms interval)
WHEN `stop()` is called 150 ms after `start()`
THEN `stop()` SHALL return within 200 ms (within one sleep chunk)

#### Scenario: Each pattern is visually distinct

GIVEN a `MockVideoProducer` in SOLID_COLOR pattern
WHEN a frame is generated
THEN all pixels SHALL have the same BGR value
GIVEN the producer in CHECKERBOARD pattern
WHEN a frame is generated
THEN the top-left 32×32 block SHALL be black (pixel value 0)
AND the adjacent 32×32 block SHALL be white (pixel value 255)

---

### Requirement: RgaVideoProducer Placeholder

`RgaVideoProducer` SHALL be a compile-time-stubbed placeholder for future RGA camera integration.

- **R-ADD-15:** `RgaVideoProducer` SHALL implement `IVideoProducer`.
- **R-ADD-16:** The implementation SHALL be guarded by `#ifdef __arm__`. On non-ARM targets (x86/Docker), the file SHALL produce a `#error` directive at the top.
- **R-ADD-17:** The `start()` method SHALL contain a `static_assert(false, "RgaVideoProducer not implemented")` so that compiling for ARM with this producer linked produces a clear error until the real implementation arrives.
- **R-ADD-18:** All other `IVideoProducer` methods SHALL be stubs that set or return `running_`.

**File:** `edge_ops/gema_vision/include/rga_video_producer.hpp` (new)

#### Scenario: Compile error on x86

GIVEN a build on an x86 machine
WHEN `rga_video_producer.hpp` is included
THEN the compiler SHALL emit `#error "RgaVideoProducer requires ARM (RV1106)"`
AND compilation SHALL fail

#### Scenario: Compile error on ARM (not yet implemented)

GIVEN a build on ARM (RV1106) with `__arm__` defined
WHEN `RgaVideoProducer::start()` is called
THEN `static_assert` SHALL fire with "RgaVideoProducer not implemented"

---

## MODIFIED Requirements

### Requirement: ThreadSafeQueue<T> (type migration)

The queue template parameter SHALL change from `cv::Mat` to `std::shared_ptr<cv::Mat>` for all pipeline usages.

(Previously: queue carried `cv::Mat` by value)

#### Scenario: Producer pushes shared_ptr to queue

GIVEN a `ThreadSafeQueue<std::shared_ptr<cv::Mat>>`
WHEN `push(std::make_shared<cv::Mat>(...))` is called
THEN `size()` SHALL equal 1
AND `pop()` SHALL return a non-null `shared_ptr`

#### Scenario: Consumer receives null shared_ptr on shutdown

GIVEN a `ThreadSafeQueue<std::shared_ptr<cv::Mat>>` with `shutdown()` called
WHEN the queue is drained
THEN `pop()` SHALL return a `shared_ptr` where `.get() == nullptr`
AND `is_shutdown()` SHALL return `true`

---

### Requirement: InferenceOrchestrator Queue Type

`InferenceOrchestrator` SHALL accept `ThreadSafeQueue<std::shared_ptr<cv::Mat>>&` instead of `ThreadSafeQueue<cv::Mat>&`.

(Previously: constructor accepted `ThreadSafeQueue<cv::Mat>&`)

- **R-MOD-1:** The constructor parameter `frame_queue` SHALL be `ThreadSafeQueue<std::shared_ptr<cv::Mat>>&`.
- **R-MOD-2:** `consumer_loop()` SHALL pop `std::shared_ptr<cv::Mat>` and pass `.get()` (dereferenced `cv::Mat&`) to `engine_.infer()`.
- **R-MOD-3:** The orchestrator SHALL NOT call `FramePool::release()` — pool release is handled by the shared_ptr deleter.
- **R-MOD-4:** The `MqttClient` forward declaration and `InferenceResult` struct SHALL remain unchanged.

**Files:**
- `edge_ops/gema_vision/include/inference_orchestrator.hpp` (modified)
- `edge_ops/gema_vision/src/inference_orchestrator.cpp` (modified)

#### Scenario: Consumer processes shared_ptr frames

GIVEN an `InferenceOrchestrator` with a mocked queue of `std::shared_ptr<cv::Mat>`
WHEN a frame is pushed as `std::make_shared<cv::Mat>(640, 480, CV_8UC3)`
AND the consumer loop runs
THEN `frames_processed()` increments by 1
AND `mqtt.publish()` is called once

---

### Requirement: Graceful Shutdown in Signal Handler

The signal handler SHALL set `g_shutdown` atomically and return — it SHALL NOT call `watchdog.stop()` or `std::_Exit()`.

(Previously: handler called `watchdog.stop()` and `std::_Exit(128 + sig)`)

- **R-MOD-5:** The signal handler SHALL only perform async-signal-safe operations: `write()` to stderr and `g_shutdown.store(true, release)`.
- **R-MOD-6:** `main()` SHALL drain orchestrator → producer → watchdog in reverse start order after the `while(!g_shutdown)` loop exits.
- **R-MOD-7:** `g_watchdog->stop()` SHALL be called from `main()`, NOT from the signal handler.
- **R-MOD-8:** `std::_Exit()` SHALL be removed. The process SHALL exit via `return 0` from `main()`.

**File:** `edge_ops/gema_vision/src/main.cpp` (modified)

#### Scenario: SIGTERM triggers clean shutdown

GIVEN the daemon running with producer and orchestrator started
WHEN `SIGTERM` is raised
THEN `g_shutdown` SHALL become `true`
AND the main loop SHALL exit
AND `orchestrator.stop()` SHALL be called
AND `producer.stop()` SHALL be called
AND `g_watchdog->stop()` SHALL be called
AND `main()` SHALL return 0

#### Scenario: Signal handler does not call _Exit

GIVEN the signal handler is installed
WHEN any signal is received
THEN `_Exit()` SHALL NOT be called
AND execution SHALL return to the interrupted context

---

### Requirement: Pipeline Tests Updated for shared_ptr Queue

All SIL tests using `ThreadSafeQueue<cv::Mat>` SHALL be updated to `ThreadSafeQueue<std::shared_ptr<cv::Mat>>`.

(Previously: tests pushed `cv::Mat::zeros(...)` to queue)

- **R-MOD-9:** `test_vision_pipeline.cpp` SHALL use `ThreadSafeQueue<std::shared_ptr<cv::Mat>>`.
- **R-MOD-10:** Frames SHALL be pushed as `std::make_shared<cv::Mat>(cv::Mat::zeros(640, 480, CV_8UC3))`.
- **R-MOD-11:** All existing assertions (frame count, publish count, mock engine output) SHALL remain valid.

**File:** `edge_ops/gema_vision/test/test_vision_pipeline.cpp` (modified)

#### Scenario: Updated test pushes shared_ptr

GIVEN the `ConsumerProcessesAllFrames` test
WHEN the test runs with `ThreadSafeQueue<std::shared_ptr<cv::Mat>>`
THEN all 5 frames SHALL be processed
AND `frames_processed()` SHALL equal 5
AND `publish_count_` SHALL equal 5

---

### Requirement: New Test — MockVideoProducer SIL Tests

A new test file SHALL verify `MockVideoProducer` behaviour in isolation.

- **R-ADD-19:** `test_video_producer.cpp` SHALL test:
  1. Start → produce N frames → stop.
  2. Patterns produce distinguishable pixel output.
  3. `set_frame_rate()` affects production speed.
  4. Stop during sleep returns promptly.
  5. `set_pattern()` overrides cycling.

**File:** `edge_ops/gema_vision/test/test_video_producer.cpp` (new)

#### Scenario: Produces exactly N frames and stops

GIVEN a `MockVideoProducer` running for 2 seconds at 10 Hz
WHEN `stop()` is called
THEN `frames_produced()` SHALL be approximately 20 (10 Hz × 2 s)
AND no frames SHALL be produced after `stop()` returns

---

### Requirement: CMakeLists.txt — New Test Target

The build system SHALL add a `test_video_producer` executable and register it with CTest.

- **R-ADD-20:** A new test target `test_video_producer` SHALL link `gema_vision_lib`, `OpenCV::opencv_core`, `GTest::GTest`, and `GTest::Main`.
- **R-ADD-21:** The test SHALL be registered with `add_test(NAME VideoProducer COMMAND test_video_producer)`.
- **R-ADD-22:** Compiler warnings (`-Wall -Wextra -Wpedantic -Werror`) SHALL apply.

**File:** `edge_ops/gema_vision/CMakeLists.txt` (modified)

#### Scenario: CTest discovers and runs new test

GIVEN the build directory is configured
WHEN `ctest --output-on-failure -V -R VideoProducer` is run
THEN the test suite SHALL run and pass
AND the test name SHALL appear in the output

---

## File Changes Summary

| File | Action | Description |
|------|--------|-------------|
| `edge_ops/gema_vision/include/video_producer.hpp` | **CREATE** | `IVideoProducer` abstract interface |
| `edge_ops/gema_vision/include/mock_video_producer.hpp` | **CREATE** | `MockVideoProducer` — header-only, 5 patterns |
| `edge_ops/gema_vision/include/rga_video_producer.hpp` | **CREATE** | `RgaVideoProducer` — `#ifdef __arm__` placeholder |
| `edge_ops/gema_vision/include/frame_pool.hpp` | **MODIFY** | Add `acquire()` returning `std::shared_ptr<cv::Mat>` |
| `edge_ops/gema_vision/include/thread_safe_queue.hpp` | Unchanged | Template, no change needed |
| `edge_ops/gema_vision/include/inference_orchestrator.hpp` | **MODIFY** | Queue type → `std::shared_ptr<cv::Mat>` |
| `edge_ops/gema_vision/src/inference_orchestrator.cpp` | **MODIFY** | Consumer loop uses `shared_ptr` deref |
| `edge_ops/gema_vision/src/main.cpp` | **MODIFY** | Wire producer + orchestrator, fix signal handler |
| `edge_ops/gema_vision/CMakeLists.txt` | **MODIFY** | Add `test_video_producer` target |
| `edge_ops/gema_vision/test/test_vision_pipeline.cpp` | **MODIFY** | Queue type → `std::shared_ptr<cv::Mat>` |
| `edge_ops/gema_vision/test/test_video_producer.cpp` | **CREATE** | MockVideoProducer SIL tests |

---

## Out of Scope

- **RGA camera hardware driver** — `RgaVideoProducer` is a compile-time stub, not a real V4L2/RGA implementation.
- **NPU inference engine** — `MockEngine` already exists; this change does not modify it.
- **FlashTrigger** — GPIO trigger is a separate concern; `MockVideoProducer` is the frame source for SIL testing.
- **Watchdog implementation** — `Watchdog` class is unchanged; only its stop() call site moves from signal handler to main().
- **Configuration file parsing** — frame rate is set programmatically via `set_frame_rate()`, not from a config file.
- **FramePool `acquire()` returning `size_t`** — may be deprecated but is not removed in this change.
- **Any changes outside `edge_ops/gema_vision/`** — `vision_edge/` and `services/` are untouched.
