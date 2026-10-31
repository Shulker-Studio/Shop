#pragma once

#include <chrono>
#include <cstdint>

namespace ss::shop {

/// 单调时钟毫秒。
///
/// 用 `steady_clock` 而不是系统时间：改系统时间不该让冷却或反刷窗口失效。
inline std::int64_t monotonicNowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

} // namespace ss::shop
