#include "util/time/WallClock.h"

#include <chrono>
#include <cstdio>
#include <ctime>

namespace ss::shop {
namespace {

/// 时区偏移写成 `+08:00` / `-05:00`。
///
/// 不用 `strftime("%z")`：MSVC 上它输出 `+0800`（没冒号）在老 CRL 里还可能受区域设置影响，
/// 而 ISO-8601 要的是带冒号的那个形状。
std::string offsetText(long offsetSeconds) {
    char const sign   = offsetSeconds < 0 ? '-' : '+';
    long const abs    = offsetSeconds < 0 ? -offsetSeconds : offsetSeconds;
    long const hours   = abs / 3600;
    long const minutes = (abs % 3600) / 60;

    char buf[8];
    std::snprintf(buf, sizeof buf, "%c%02ld:%02ld", sign, hours, minutes);
    return std::string{buf};
}

} // namespace

std::string nowIso8601Local() {
    std::time_t const now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

    std::tm local{};
#if defined(_WIN32)
    if (localtime_s(&local, &now) != 0) {
        return {};
    }
#else
    if (localtime_r(&now, &local) == nullptr) {
        return {};
    }
#endif

    char dateTime[32];
    if (std::strftime(dateTime, sizeof dateTime, "%Y-%m-%dT%H:%M:%S", &local) == 0) {
        return {};
    }

    // 时区偏移：把「本地墙上时间」当成 UTC 再解释一次，两者的差就是偏移（含夏令时）。
    // 不读 `tm_gmtoff`（Windows 的 `std::tm` 没有这个成员），也不用两次 `mktime`
    // （夏令时的重复小时会让它自己跟自己打起来）。
    long offsetSeconds = 0;
#if defined(_WIN32)
    std::time_t const localAsUtc = _mkgmtime(&local);
#else
    std::time_t const localAsUtc = timegm(&local);
#endif
    if (localAsUtc != static_cast<std::time_t>(-1)) {
        offsetSeconds = static_cast<long>(localAsUtc - now);
    }

    return std::string{dateTime} + offsetText(offsetSeconds);
}

} // namespace ss::shop
