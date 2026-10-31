#include "shop/Economy.h"

#include <limits>
#include <utility>

namespace ss::shop::economy {
namespace {

/// 后端名 ↔ 枚举。配置里能写哪些名字由 DESIGN §8.4 决定，只在这张表里列一次。
constexpr std::pair<Backend, std::string_view> kBackendNames[] = {
    {Backend::Auto,        "auto"        },
    {Backend::LegacyMoney, "legacy_money"},
    {Backend::Scoreboard,  "scoreboard"  },
    {Backend::Builtin,     "builtin"     },
    {Backend::Null,        "null"        },
};

} // namespace

std::optional<Backend> parseBackend(std::string_view name) {
    for (auto const& [backend, backendName] : kBackendNames) {
        if (backendName == name) {
            return backend;
        }
    }
    return std::nullopt;
}

std::string_view backendName(Backend backend) {
    for (auto const& [candidate, name] : kBackendNames) {
        if (candidate == backend) {
            return name;
        }
    }
    return {};
}

std::string backendNamesText(std::string_view separator) {
    std::string text;
    for (auto const& [backend, name] : kBackendNames) {
        if (!text.empty()) {
            text += separator;
        }
        text += name;
    }
    return text;
}

TransferResult
EconomyProvider::transfer(Account const& from, Account const& to, std::int64_t amount, bool allowNegative) {
    if (amount <= 0) {
        return {TransferStatus::Rejected, "转账金额必须大于 0"};
    }
    if (from == to) {
        return {TransferStatus::Rejected, "不能转给同一个账户"};
    }
    if (!reduce(from, amount, allowNegative)) {
        return {TransferStatus::Rejected, "付款方扣款失败（余额不足或账户不可记账）"};
    }
    if (add(to, amount)) {
        return {TransferStatus::Ok, {}};
    }

    // 钱已经从付款方身上扣掉了：必须加回去，否则就是凭空消失。
    if (add(from, amount)) {
        return {TransferStatus::Rejected, "收款方不可记账，款已退回付款方"};
    }
    return {TransferStatus::Broken, "收款方不可记账，且退款也失败"};
}

std::optional<int> toScoreValue(std::int64_t amount) {
    if (amount < std::numeric_limits<int>::min() || amount > std::numeric_limits<int>::max()) {
        return std::nullopt;
    }
    return static_cast<int>(amount);
}

} // namespace ss::shop::economy
