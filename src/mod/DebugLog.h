#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace ss::shop {

/// `/shop debug on|off` 的开关。
///
/// 故意**不持久化**，也不做成 `config.json` 的键：配置要重启才生效，而调试的全部价值
/// 就是"现在就想看"。模组关闭时由 `Shop::disable()` 归零。
[[nodiscard]] bool shopDebugEnabled();

void setShopDebugEnabled(bool enabled);

/// 拼一个 `k=v` 字段。值里出现空格会让人眼读起来歧义，所以值本身的空格会换成 `_`。
std::string dbgField(std::string_view key, std::string_view value);
/// `char const*` 必须有**精确匹配**的重载。
///
/// 没有它的时候，`dbgField("kind", "hopper_pull")` 与 `dbgField("plan", plan)` 会静默选中
/// `bool` 重载——指针唯一的隐式转换目标就是 `bool`——于是日志里打出 `kind=true`。
/// 真机已经两次踩到这个（`plan`、`kind`），所以这里给 `char const*` 一个精确匹配，
/// 让重载决议压根走不到 `bool` 上去。传 C 字符串不再需要写 `std::string_view{...}`。
std::string dbgField(std::string_view key, char const* value);

/// `int` 单独一个重载：`int` 同时能转 `long long` 与 `bool`（都是 Conversion 级），
/// 少这个重载，`dbgField("dim", getDimensionId())` 这类调用会直接报歧义。
std::string dbgField(std::string_view key, int value);
std::string dbgField(std::string_view key, long long value);
std::string dbgField(std::string_view key, bool value);

/// `dim=3 x=1000 y=100 z=1000`，一格方块的位置。
std::string dbgPos(int dimensionId, int x, int y, int z);

/// 只在开关打开时输出一行：`[shop/dbg] <kind> <字段…>`。
///
/// 字段是**先拼好再传进来**的，所以在会被每 tick 调到的热路径（漏斗、投掷器、铜傀儡）上，
/// 调用方要先自己 `if (!shopDebugEnabled()) { return …; }` 再拼字段——别让一份关掉的
/// 调试日志把字符串拼接留在红石时钟里。
void shopDebugEvent(std::string_view kind, std::vector<std::string> const& fields = {});

/// 与上面相同，但**连续重复的同一行在 1 秒内只出一次**。
///
/// 专门给会被每 tick 走到的位置（漏斗、投掷器、铜傀儡）用：红石时钟下一次拦截每秒会触发 20 次，
/// 翻成 20 行相同的文字只会把控制台刷满。只比「连续相同」，所以换一格、换一类照样都记。
void shopDebugEventThrottled(std::string_view kind, std::vector<std::string> const& fields = {});

} // namespace ss::shop
