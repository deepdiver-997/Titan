#include "timing_wheel.h"

// 每个 level 的槽跨度（ticks/slot）
// level 0: 1 tick    — 直接到期执行
// level 1: wheel_size ticks — 一圈 level-0
// level 2: wheel_size^2 ticks
// level 3: wheel_size^3 ticks
static size_t slot_span(int wheel_size, int level) {
    size_t s = 1;
    for (int i = 0; i < level; ++i) s *= wheel_size;
    return s;
}

// 从 expire_tick 计算某级槽号
// tick() 在 tick_count_==E 时处理的 cursor = (E-1)，级联点的 cursor = (E/span - 1)
// 统一公式: slot = (expire_tick / span - 1) % wheel_size
static size_t expire_to_slot(size_t expire_tick, int wheel_size, int level) {
    size_t span = slot_span(wheel_size, level);
    return ((expire_tick / span) - 1) % wheel_size;
}

TimingWheel::TimingWheel(int tick_interval_ms, int wheel_size)
    : tick_interval_ms_(tick_interval_ms)
    , wheel_size_(wheel_size)
{
    for (int i = 0; i < kLevels; ++i) {
        slots_[i].resize(wheel_size);
    }
}

uint64_t TimingWheel::addTask(int delay_ms, std::function<void()> task) {
    if (delay_ms <= 0) delay_ms = tick_interval_ms_;

    size_t total_ticks = (delay_ms + tick_interval_ms_ - 1) / tick_interval_ms_;
    size_t expire = tick_count_ + total_ticks;

    Task t{next_id_.fetch_add(1, std::memory_order_relaxed), expire, std::move(task)};
    reschedule(std::move(t));
    ++pending_count_;
    return t.id;
}

void TimingWheel::reschedule(Task&& task) {
    size_t remaining = (task.expire_tick > tick_count_) ? (task.expire_tick - tick_count_) : 0;

    if (remaining == 0) {
        if (cancelled_.count(task.id)) {
            cancelled_.erase(task.id);
            processed_.insert(task.id);
            return;
        }
        --pending_count_;
        processed_.insert(task.id);
        task.callback();
        return;
    }

    for (int level = 0; level < kLevels; ++level) {
        size_t span = slot_span(wheel_size_, level);
        if (remaining < static_cast<size_t>(wheel_size_) * span || level == kLevels - 1) {
            size_t slot = expire_to_slot(task.expire_tick, wheel_size_, level);
            slots_[level][slot].push_back(std::move(task));
            return;
        }
    }
}

bool TimingWheel::cancelTask(uint64_t task_id) {
    // 已取消或已执行完成 → 拒绝重复取消
    if (cancelled_.count(task_id) || processed_.count(task_id)) return false;
    cancelled_.insert(task_id);
    --pending_count_;
    return true;
}

void TimingWheel::tick() {
    ++tick_count_;

    // 1. 执行 level-0 当前槽（全部到期）
    auto& slot0 = slots_[0][cursors_[0]];
    auto it0 = slot0.begin();
    while (it0 != slot0.end()) {
        if (cancelled_.count(it0->id)) {
            cancelled_.erase(it0->id);
            processed_.insert(it0->id);
            it0 = slot0.erase(it0);
        } else {
            auto cb = std::move(it0->callback);
            uint64_t id = it0->id;
            it0 = slot0.erase(it0);
            --pending_count_;
            processed_.insert(id);
            cb();
        }
    }

    cursors_[0] = (cursors_[0] + 1) % wheel_size_;

    // 2. 级联
    for (int level = 0; level < kLevels - 1; ++level) {
        if (cursors_[level] == 0) {
            auto& high_slot = slots_[level + 1][cursors_[level + 1]];
            for (auto& task : high_slot) {
                if (cancelled_.count(task.id)) {
                    cancelled_.erase(task.id);
                    processed_.insert(task.id);
                    continue;
                }
                reschedule(std::move(task));
            }
            high_slot.clear();
            cursors_[level + 1] = (cursors_[level + 1] + 1) % wheel_size_;
        } else {
            break;
        }
    }

    // 3. 定期 GC：每 wheel_size 个 tick 清理已完成的 processed_ 记录
    if (tick_count_ % static_cast<size_t>(wheel_size_) == 0) {
        gc();
    }
}

void TimingWheel::gc() {
    // processed_ 大小超过阈值时清理，保留最近 wheel_size 轮的记录
    if (processed_.size() > static_cast<size_t>(wheel_size_ * 100)) {
        processed_.clear();
    }
}

size_t TimingWheel::pendingTaskCount() const {
    return pending_count_;
}
