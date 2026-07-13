#pragma once

#include <cstdint>

namespace bthread_timer {

using TaskId = uint64_t;
constexpr TaskId INVALID_TASK_ID = 0;

// Callback type: void (*)(void*)
// Callbacks should be fast and non-blocking. The ideal usage is to
// post a task to another thread for actual work, preventing the
// timer thread from being blocked.
using TimerCallback = void (*)(void*);

// ---- Task flags ----------------------------------------------------------

// Default — execution time counts toward wall-clock.
constexpr uint32_t TASK_FLAG_NONE = 0;

// When set, the elapsed wall-clock time of this task is accumulated
// into g_compensation_us so that subsequent deadline checks use
// compensated (slowed-down) time.  Effectively "freezes" the virtual
// clock while the task runs.
//
// Use case: snapshotting the entire ActorSystem — all time wheels
// appear paused because their tick callbacks see a compensated
// now_us() that barely advanced.
constexpr uint32_t TASK_FLAG_DONT_COUNT_TIME = 1;

// ---- Per-thread time compensation (read/written by timer thread) --------

// Accumulated microseconds to subtract from now_us() when checking
// deadlines.  Only the bthread_timer thread writes this; any thread
// may read it for external wall-clock timeout compensation.
extern thread_local int64_t tls_compensation_us;

}  // namespace bthread_timer
