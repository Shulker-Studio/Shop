#pragma once

#include <chrono>
#include <cstdint>
#include <string>

namespace ss::shop {

/// 单调时钟毫秒。
///
/// 用 `steady_clock` 而不是系统时间：改系统时间不该让冷却或反刷窗口失效。
inline std::int64_t monotonicNowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

/// 服务器本地时间，ISO-8601 带时区偏移：`2026-11-01T13:23:12+08:00`。
///
/// 用系统时间（`system_clock`）而不是 `monotonicNowMs` 那个单调时钟：留档是给人看的台账，
/// 「几点几分」必须跟服务器的钟对得上。改系统时间会让它跳，这正是台账想要的语义。
std::string nowIso8601Local();

} // namespace ss::shop
