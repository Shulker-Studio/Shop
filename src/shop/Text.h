#pragma once

#include <cctype>
#include <string>
#include <string_view>

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

} // namespace ss::shop
