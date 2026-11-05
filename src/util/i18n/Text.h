#pragma once

#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace ss::shop {

/// 去掉两端的空白（`std::isspace` 那一套：空格 / 制表 / 换行 / 回车 …）。
///
/// 牌面文本与表单输入都从玩家手里来，两端带空白是常态。三处（牌面解析、份数表单、
/// 管理表单）都需要这一个动作，所以只有这一份实现：两份的话，迟早对「什么算空白」
/// 各说各话，同一段输入在一个入口能过、在另一个入口不能过。
inline std::string_view trimView(std::string_view text) {
    auto const isSpace = [](char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; };
    while (!text.empty() && isSpace(text.front())) {
        text.remove_prefix(1);
    }
    while (!text.empty() && isSpace(text.back())) {
        text.remove_suffix(1);
    }
    return text;
}

/// 同上的副本版本（需要把结果留在字符串里时用）。
inline std::string trimCopy(std::string_view text) { return std::string{trimView(text)}; }

/// 一段文本的语义角色，决定渲染时的颜色。
///
/// `Default` 表示「不设色，继承前一段的颜色」——表单的 `field()` 靠它让冒号
/// 跟标签同色，而不是在每段之间来回插复位码。
enum class Tone {
    Default,
    Label,
    Value,
    Accent,
    Good,
    Bad,
    Warn,
    Muted,
    Title,
};

struct Text;

/// 模板参数（递归：参数本身也可能是一段带 key/args 的文本）。
using TextArgs = std::vector<Text>;

/// 一段文本。**纯逻辑层只负责产出结构，不负责把它拼成字符串**：
///
/// - `key` 非空：查词表拿模板（`"还差 {} 元"`），再用 `args` 填 `{}`；
/// - `key` 为空：`literal` 原样输出（数字、玩家名、金额这些不需要翻译的片段）。
///
/// 渲染由 `render()` 做，实际格式化器是**注入**的：模组启用时 `util/i18n/Locale.cpp`
/// 注入 `ll::i18n` + `fmt`，单元测试注入自己那份。纯逻辑层因此可以 include
/// `fmt` 之外什么都不用，也不会违反 `check_pure_logic_headers`。
struct Text {
    Tone        tone = Tone::Default;
    std::string key;
    std::string literal;
    TextArgs    args;
    /// `args` 之间的关系：`false` = 首尾相接（`sequence`），`true` = 每段一行（`block`）。
    bool multiline = false;
};

namespace text {

/// 文本查表的注入点。
///
/// 纯逻辑层不能 include `ll/api/i18n`（构建期的 `check_pure_logic_headers` 会拦），
/// 所以这里只留一个函数指针：模组启用时由 `util/i18n/Locale.cpp` 注入 `ll::i18n` 的查表，
/// 单元测试与「模组没加载」时没人注入，`tr` 就原样返回 key。
using Lookup = std::string_view (*)(std::string_view key);

/// 渲染器的注入点。签名是「一段文本 → 已上色的字符串」。
///
/// 由 `util/i18n/Locale.cpp` 用 `ll::i18n` 的模板 + `fmt::vformat` 实现；测试注入自己那份。
/// 没注入时退回 `tr(key)`，只保证「能读」，不保证 `{}` 被填。
using Renderer = std::string (*)(Text const& text, std::string_view localeCode);

void installLookup(Lookup lookup);
void installRenderer(Renderer renderer);

/// 查一条文本；没有注入查表、或查不到时原样返回 key。
std::string tr(std::string_view key);

/// 每个 `Tone` 对应的颜色码。`Default` 返回空串（继承前一段的颜色）。
std::string_view colorOf(Tone tone);

/// 渲染一段文本（含 `Tone` 对应的颜色前缀，**不含**行尾复位）。
std::string render(Text const& text, std::string_view localeCode = {});

/// 多行渲染：每行一个 `Text`，行间用 `\n`，末尾补一次复位。
std::string renderLines(std::vector<Text> const& lines, std::string_view localeCode = {});

/// 这一段有没有内容。
inline bool empty(Text const& text) {
    return text.key.empty() && text.literal.empty() && text.args.empty();
}

/// 原样文本（数字、玩家名、金额）。
Text lit(std::string value, Tone tone = Tone::Default);

/// 词表文本，可带 `{}` 参数。
Text keyed(std::string key, Tone tone = Tone::Default, TextArgs args = {});

/// `标签：值` 一行。标签灰色、值白色、分隔符跟标签同色。
Text field(Text label, Text value);

/// 同上，但分隔符是空格（`选 3 份 →  共 192 个` 这类没有冒号的行）。
Text joined(Text label, Text value);

/// 把几段文本排成多行（表单正文）：渲染时行间插换行。
Text block(std::vector<Text> lines);

/// 把几段文本首尾相接（`field(...) + muted(...)` 那种行内拼接）。
///
/// 渲染时按顺序展开，每段保留自己的颜色。
Text sequence(TextArgs parts);

/// 分区小标题（「这家店暂停营业」这类），金色。
Text heading(Text body);

/// 次要说明（提示语、括号里的限制），深灰。
Text muted(Text body);

/// 分组标题：`──── 价格 ────`。
///
/// 表单里的字段一多，一眼扫过去全是同宽的行，找「哪几行是价格」要靠读字。
/// 一条灰线加一个短标题把正文切成几段，比给每一行换颜色有效得多。
/// 标题本身用 `Tone::Value`（白），线用 `Tone::Muted`（灰），层级自然分明。
Text section(Text title);

/// 颜色码。表单正文在纯逻辑层产出，所以颜色也只能在这里定——
/// 但**渲染**在 mod 层做，mod 层用 `mc/util/ColorFormat.h` 的常量；
/// 这里写字面量是因为纯逻辑层不能 include `mc/`（构建期守卫会拦）。
///
/// 这些值与 `ColorFormat` 的 `AQUA` / `WHITE` / `GOLD` / `GREEN` / `RED` /
/// `YELLOW` / `GRAY` / `BOLD` / `RESET` 逐字相同，改配色时**两处一起改**。
///
/// 正文的配色约定（一行一个字段）：标签青、值白、钱金/绿、不可交易原因红、
/// 提示灰。标签与值必须**不同色系**，否则在表单的深色底上只是一片灰白。
///
/// 牌面文本（`formatShopSign`）**不用**这些：牌面上的 `§` 是合法颜色码，
/// 但那里是数据不是外观，改了会牵动解析。
inline constexpr std::string_view kLabelColor  = "§b"; // 青：标签
inline constexpr std::string_view kValueColor  = "§f"; // 白：值
inline constexpr std::string_view kAccentColor = "§6"; // 金：标题 / 管理入口
inline constexpr std::string_view kGoodColor   = "§a"; // 绿：买入 / 成功
inline constexpr std::string_view kBadColor    = "§c"; // 红：卖出 / 失败
inline constexpr std::string_view kWarnColor   = "§e"; // 黄：警告 / 停业
inline constexpr std::string_view kMutedColor  = "§7"; // 灰：次要 / 提示 / 取消
inline constexpr std::string_view kReset       = "§r";
/// 表单标题：加粗金。
///
/// **顺序不能反**：颜色码会清掉它之前的格式码（MC 的既有行为，`§nX§cY` 里的 Y 不再是下划线），
/// 所以必须是「先颜色、后格式」（`§6§l`）。写成 `§l§6` 时粗体当场被 `§6` 清掉，只剩金色。
inline constexpr std::string_view kTitleColor  = "§6§l";

} // namespace text

} // namespace ss::shop
