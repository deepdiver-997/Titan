#pragma once

#include <cstdint>

namespace gs {

struct ServerConfig {
    // ---- Network -------------------------------------------------------
    const char* listen_address = "127.0.0.1";
    uint16_t listen_port = 8888;
    int max_connections = 1024;

    // ---- AOI -----------------------------------------------------------
    int grid_cell_size = 16;       // 格子边长（米）
    int aoi_radius = 2;            // 视野半径（格），总计 (2*radius+1)^2 格
    int scene_width = 6400;        // 场景宽度（米）= 400 格
    int scene_height = 6400;       // 场景高度（米）

    // ---- Tick frequencies (Hz) -----------------------------------------
    int tick_move = 30;            // 移动 tick
    int tick_skill = 60;           // 技能 tick
    int tick_aoi = 10;             // AOI 刷新 tick
    int tick_net_sync = 30;        // 网络同步 tick

    // ---- Cross-scene ------------------------------------------------
    int scene_boundary_margin = 2;  // 边界区域宽度（格），进入此区域开始双注册
    int scene_hysteresis = 3;      // 滞回阈值（格），离开边界 >3 格才退订

    // ---- Connection ----------------------------------------------------
    int heartbeat_interval_ms = 1000;   // 心跳间隔
    int conn_timeout_ms = 5000;         // 连接超时

    // ---- TimingWheel ---------------------------------------------------
    int tw_tick_interval_ms = 16;  // 每 tick 毫秒 (60Hz = ~16.67ms)
    int tw_wheel_size = 256;       // 时间轮槽数

    // ---- bthread_timer ----------------------------------------------
    int bt_num_buckets = 13;
    int bt_task_pool_size = 131072;
};

}  // namespace gs
