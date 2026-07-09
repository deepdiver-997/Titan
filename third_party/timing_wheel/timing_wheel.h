#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

/// 时间轮定时器 — skynet 多级级联实现
///
/// 4 级时间轮，每级 wheel_size 个槽。每 tick 只遍历 level-0 当前槽的任务
/// （保证全部到期），避免了 rounds 计数字段导致的"逐任务判断是否到期"浪费。
/// 当低级轮走完一圈时，从高级轮级联搬移任务到低级轮。
class TimingWheel {
public:
    /// @param tick_interval_ms 每 tick 毫秒数
    /// @param wheel_size       每级槽位数
    explicit TimingWheel(int tick_interval_ms, int wheel_size);

    /// 添加定时任务
    /// @param delay_ms 延迟毫秒
    /// @param task     到期回调
    /// @return 任务 ID（用于取消）
    uint64_t addTask(int delay_ms, std::function<void()> task);

    /// 取消定时任务（惰性标记，不立即从链表删除）
    /// @return 是否成功取消
    bool cancelTask(uint64_t task_id);

    /// 推进一个 tick：执行到期任务，必要时级联搬移
    void tick();

    /// 已注册但未执行的任务数（含已被惰性取消但尚未清理的）
    size_t pendingTaskCount() const;

    /// 清理已完成的 cancelled_/processed_ 记录（定期调用，防止无限增长）
    void gc();

private:
    static constexpr int kLevels = 4;

    struct Task {
        uint64_t id;
        size_t expire_tick; // 绝对 tick 编号，tick_count 到达时执行
        std::function<void()> callback;
    };

    // 重新分配任务到最合适的 level/slot（用于级联搬移）
    void reschedule(Task&& task);

    // 当前 tick 计数
    size_t tick_count_ = 0;

    int tick_interval_ms_;
    int wheel_size_;

    // 每级指针当前指向的槽位
    size_t cursors_[kLevels] = {};

    // 每级的槽数组：slots_[level][slot_index]
    std::vector<std::list<Task>> slots_[kLevels];

    // 惰性取消集合（待清理）
    std::unordered_set<uint64_t> cancelled_;
    // 已完成集合（执行或级联时已清理），防止重复 cancel 误判
    std::unordered_set<uint64_t> processed_;

    std::atomic<uint64_t> next_id_{1};
    size_t pending_count_ = 0;
};
