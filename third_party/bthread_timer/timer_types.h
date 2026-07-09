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

}  // namespace bthread_timer
