#pragma once

#include "util/i18n/Locale.h"

#include "mc/util/ColorFormat.h"

#include "fmt/format.h"

#include <string>
#include <string_view>
#include <utility>

class Player;

namespace ss::shop::ui {

/// 玩家可见消息的外观（DESIGN §12 的 P2「i18n 与文本」）。
///
/// 前缀只有这一份：以前 `/shop` 的消息里只有一处写了 `§c[Shop]`，其余全是裸文本，
/// 单看聊天记录分不清哪条是商店发的。语义色也在这里，不在每个调用点各挑一个红。
///
/// 颜色码一律取自 `mc/util/ColorFormat.h`（`§8` 这类码是 BDS 定义好的常量），
/// 不在本模组里再写一遍字面量。
inline constexpr std::string_view kSuccess = ColorFormat::GREEN;
inline constexpr std::string_view kFailure = ColorFormat::RED;
inline constexpr std::string_view kWarning = ColorFormat::YELLOW;
inline constexpr std::string_view kHint    = ColorFormat::GRAY;

/// `§8[§6Shop§8]§r `：拼接要跑一遍，所以不能是 `constexpr` 常量。
inline std::string prefix() {
    std::string out;
    out += ColorFormat::DARK_GRAY;
    out += '[';
    out += ColorFormat::GOLD;
    out += "Shop";
    out += ColorFormat::DARK_GRAY;
    out += ']';
    out += ColorFormat::RESET;
    out += ' ';
    return out;
}

enum class Tone { Success, Failure, Warning, Hint };

/// 后台日志用：查 `key` 的模板，把每个参数按 `fmt` 规则转成文本填进 `{}`，**去掉颜色码**。
///
/// 日志不是玩家界面：`§8` 混进去只会污染 `rg`、JSON 与告警规则。但文案本身仍进词表，
/// 免得聊天里的措辞改了、日志里还留着旧版本。
/// 参数的 `fmt` 格式交给调用方的类型（`int` 走十进制、`bool` 走 true/false），
/// 需要特殊写法时先在调用点拼好字符串再传。
template <class... Args>
std::string msg(std::string_view key, Args&&... args) {
    ss::shop::TextArgs parts;
    parts.reserve(sizeof...(Args));
    (parts.push_back(text::lit(fmt::format("{}", std::forward<Args>(args)))), ...);
    return plain(text::keyed(std::string{key}, ss::shop::Tone::Default, std::move(parts)));
}

/// 把一行内容套上前缀与语义色（命令输出、日志以外的玩家可见文本）。
std::string line(Tone tone, std::string_view body);

/// 同上，但 body 是结构化文本：按服务器默认 locale 渲染后再套前缀。
std::string line(Tone tone, ss::shop::Text const& text);

/// 直接发给玩家：按这名玩家的语言翻译，再套前缀与颜色。
void send(Player& player, Tone tone, std::string_view chinese);

/// 已经结构化的文本：按**这名玩家**的语言渲染，再套前缀与语义色。
void send(Player& player, Tone tone, ss::shop::Text const& text);

} // namespace ss::shop::ui
