#pragma once

#include <cstdint>
#include <mutex>
#include <vector>

namespace gs {

// Thread-safe receive buffer — network thread writes, tick thread swaps.
class RecvBuffer {
public:
    void append(const uint8_t* data, size_t len) {
        std::lock_guard<std::mutex> lk(_mutex);
        _buf.insert(_buf.end(), data, data + len);
    }

    // Atomically take all buffered data, leaving the buffer empty.
    std::vector<uint8_t> swap_out() {
        std::vector<uint8_t> out;
        {
            std::lock_guard<std::mutex> lk(_mutex);
            out.swap(_buf);
        }
        return out;
    }

    size_t size() const {
        std::lock_guard<std::mutex> lk(_mutex);
        return _buf.size();
    }

private:
    mutable std::mutex _mutex;
    std::vector<uint8_t> _buf;
};

}  // namespace gs
