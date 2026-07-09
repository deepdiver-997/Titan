#pragma once

#include "gs/common/config.h"

#include <cstdint>
#include <functional>
#include <vector>

namespace gs {

// Layered tick scheduler.
//
// Drives game logic at a base frequency (the LCM of all tick layers).
// Each layer fires at its own frequency: e.g. base=60Hz, move=30Hz fires
// every 2nd tick, AOI=10Hz every 6th tick.
//
// This runs on the Scene Actor's thread — no locks needed.
//
// The TimingWheel is used separately for per-entity timers (skill cooldowns,
// buff durations, etc.), not for the frame loop itself. See Scene class.
class TickManager {
public:
    struct Layer {
        const char* name;
        int frequency_hz;                 // target frequency
        std::function<void(int64_t tick)> callback;
        int interval = 0;                 // ticks between fires (computed)
    };

    // @param base_frequency_hz  base tick frequency (should be >= all layers)
    explicit TickManager(int base_frequency_hz);

    // Register a tick layer. Callbacks fire at `frequency_hz`.
    void add_layer(const char* name, int frequency_hz,
                   std::function<void(int64_t tick)> callback);

    // Advance one tick. Fires all layers whose tick count divides evenly.
    void tick();

    // Total ticks elapsed.
    int64_t tick_count() const { return _tick_count; }

private:
    int _base_frequency_hz;
    int64_t _tick_count = 0;
    std::vector<Layer> _layers;
};

}  // namespace gs
