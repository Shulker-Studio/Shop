#pragma once

#include "util/i18n/Text.h"

#include <string>
#include <string_view>

namespace ss::shop::ui {

/// 载入 `lang/*.json`（文件名就是 locale code，例如 `zh_CN.json`）。
///
/// 没有 `lang/` 目录、或某个文件解析失败，都只打一条日志然后继续：**代码里的中文原文
/// 就是默认语言**，所以缺语言文件不会让任何功能失效。
///
/// 同时把查表与渲染注入 `text::`，纯逻辑层的 `Text` 树因此能被渲染。
void loadTexts();

/// 把 `Text` 树渲染成带颜色的字符串。
///
/// 模板查 `ll::i18n`（key → 模板串），`{}` 用 `fmt` 填，颜色取 `text::colorOf`。
/// 表单正文、按钮文案都走这里。
std::string renderText(ss::shop::Text const& text, std::string_view localeCode);

/// 把一句中文原文翻成目标语言；没有对应条目时返回原文。
///
/// key 就是中文原文本身（LeviLimina 的 i18n 约定），所以新增文案不需要先去登记一个 id，
/// 漏翻的结果是「这句还是中文」，而不是「显示成键名」。
std::string translate(std::string_view chinese, std::string_view localeCode);

/// 把 `Text` 渲染成**不带任何颜色码**的纯文本，按服务器默认 locale。
///
/// 后台日志用这个：日志文件里混进 `§8` 只会污染 `rg` 与 JSON 检索，而玩家看不见它。
/// 模板与参数替换跟玩家消息走同一条 `renderText`，所以不会出现「聊天里是新的、
/// 日志里还是旧的」两套文案。
std::string plain(ss::shop::Text const& text);

} // namespace ss::shop::ui
