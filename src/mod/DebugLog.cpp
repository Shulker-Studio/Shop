#include "mod/DebugLog.h"

#include "mod/Shop.h"

#include <chrono>

namespace ss::shop {
namespace {

/// 开关状态只在服务端主逻辑里读写（表单、命令、事件都跑在服务器的线程上），
/// 一个 `bool` 够用，不需要原子操作。
bool gDebugEnabled = false;

/// 节流只关心「连续重复」，两条状态就够：上一条整行文本 + 它的时间。
std::string                      gLastLine;
std::chrono::steady_clock::time_point gLastAt;

/// 值里出现空白会让 `k=v k=v` 的一行读起来分不清边界，替换掉。
std::string sanitize(std::string_view value) {
    std::string out;
    out.reserve(value.size());
    for (char const c : value) {
        out.push_back((c == ' ' || c == '\t' || c == '\n' || c == '\r') ? '_' : c);
    }
    return out;
}

} // namespace

bool shopDebugEnabled() { return gDebugEnabled; }

void setShopDebugEnabled(bool enabled) {
    gDebugEnabled = enabled;
    // 关掉再打开之后，同样的第一行不该被上一次的节流状态吞掉。
    gLastLine.clear();
    gLastAt = {};
}

std::string dbgField(std::string_view key, std::string_view value) {
    std::string out{key};
    out.push_back('=');
    out.append(sanitize(value));
    return out;
}

std::string dbgField(std::string_view key, char const* value) {
    return dbgField(key, std::string_view{value == nullptr ? "" : value});
}

std::string dbgField(std::string_view key, int value) { return dbgField(key, std::to_string(value)); }

std::string dbgField(std::string_view key, long long value) { return dbgField(key, std::to_string(value)); }

std::string dbgField(std::string_view key, bool value) { return dbgField(key, std::string_view{value ? "true" : "false"}); }

std::string dbgPos(int dimensionId, int x, int y, int z) {
    std::string out = "dim=" + std::to_string(dimensionId);
    out += " x=" + std::to_string(x);
    out += " y=" + std::to_string(y);
    out += " z=" + std::to_string(z);
    return out;
}

void shopDebugEvent(std::string_view kind, std::vector<std::string> const& fields) {
    if (!gDebugEnabled) {
        return;
    }
    std::string line{"[shop/dbg] "};
    line.append(kind);
    for (auto const& field : fields) {
        line.push_back(' ');
        line += field;
    }
    // 走 `std::string&&` 那个重载：明细里有玩家名、物品名、玩家看到的提示，
    // 里面出现 `{}` 时不该被当成格式串（那是 fmt 的语法，不是我们的）。
    Shop::getInstance().getSelf().getLogger().info(std::move(line));
}

void shopDebugEventThrottled(std::string_view kind, std::vector<std::string> const& fields) {
    if (!gDebugEnabled) {
        return;
    }
    std::string line{"[shop/dbg] "};
    line.append(kind);
    for (auto const& field : fields) {
        line.push_back(' ');
        line += field;
    }

    auto const now = std::chrono::steady_clock::now();
    if (line == gLastLine && now - gLastAt < std::chrono::seconds{1}) {
        return;
    }
    gLastLine = line;
    gLastAt   = now;
    Shop::getInstance().getSelf().getLogger().info(std::move(line));
}

} // namespace ss::shop
