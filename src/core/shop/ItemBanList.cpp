#include "core/shop/ItemBanList.h"

namespace ss::shop {
namespace {

std::string trim(std::string const& text) {
    auto const first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    auto const last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

} // namespace

ItemBanList::ItemBanList(std::vector<std::string> const& patterns) {
    mRules.reserve(patterns.size());
    for (auto const& raw : patterns) {
        auto const pattern = trim(raw);
        if (pattern.empty()) {
            continue; // 空规则 = 没写，不是错误（改配置时留一行空字符串很常见）
        }
        try {
            // `optimize` 让 `regex_match` 不用每次现建自动机：这份表在点牌与提交时都会被问。
            mRules.emplace_back(pattern, std::regex::ECMAScript | std::regex::icase | std::regex::optimize);
        } catch (std::regex_error const&) {
            mInvalid.push_back(pattern);
        }
    }
}

bool ItemBanList::banned(std::string_view itemId) const {
    if (itemId.empty() || mRules.empty()) {
        return false;
    }
    for (auto const& rule : mRules) {
        if (std::regex_match(itemId.begin(), itemId.end(), rule)) {
            return true;
        }
    }
    return false;
}

} // namespace ss::shop
