#include "gs/tick/tick_manager.h"

namespace gs {

TickManager::TickManager(int base_frequency_hz)
    : _base_frequency_hz(base_frequency_hz) {}

void TickManager::add_layer(const char* name, int frequency_hz,
                            std::function<void(int64_t tick)> callback) {
    Layer layer;
    layer.name = name;
    layer.frequency_hz = frequency_hz;
    layer.callback = std::move(callback);
    layer.interval = _base_frequency_hz / frequency_hz;
    if (layer.interval < 1) layer.interval = 1;
    _layers.push_back(std::move(layer));
}

void TickManager::tick() {
    ++_tick_count;
    for (auto& layer : _layers) {
        if (_tick_count % layer.interval == 0) {
            layer.callback(_tick_count);
        }
    }
}

}  // namespace gs
