#include "core/economy/Provider.h"

#include <limits>
#include <utility>

namespace ss::shop::economy {
namespace {

/// 后端名 ↔ 枚举。配置里能写哪些名字由 DESIGN §8.4 决定，只在这张表里列一次。
constexpr std::pair<Backend, std::string_view> kBackendNames[] = {
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

std::string legalBackendNamesText(std::string_view separator) {
    std::string text;
    for (auto const& [backend, name] : kBackendNames) {
        if (backend == Backend::Builtin) {
            continue; // P1：写了会被拒绝加载，不算「合法取值」
        }
        if (!text.empty()) {
            text += separator;
        }
        text += name;
    }
    return text;
}

BackendProbes probesFor(Backend configured) {
    switch (configured) {
    case Backend::LegacyMoney:
        return {true, false};
    case Backend::Scoreboard:
        return {false, true};
    case Backend::Builtin:
    case Backend::Null:
        break;
    }
    return {};
}

Text probeIssueText(ProbeIssue issue) {
    switch (issue) {
    case ProbeIssue::ModuleMissing:
        return text::keyed("shop.economy.issue.module_missing");
    case ProbeIssue::ExportsMissing:
        return text::keyed("shop.economy.issue.exports_missing");
    case ProbeIssue::ObjectiveMissing:
        return text::keyed("shop.economy.issue.objective_missing");
    case ProbeIssue::ObjectiveCreateFailed:
        return text::keyed("shop.economy.issue.objective_create_failed");
    case ProbeIssue::None:
        break;
    }
    return text::keyed("shop.economy.issue.none");
}

BackendSelection selectBackend(Backend configured, BackendCandidates const& candidates) {
    BackendSelection selection;

    // 空实现不需要探测：「一个能记账的后端都没有」就是它的定义。
    // `builtin`（P1）同理：配置校验已经拒了它，万一漏到这儿也不拿别的后端顶替——
    // 服主写了 builtin 就是不想要另外两个后端记账。
    if (configured == Backend::Null || configured == Backend::Builtin) {
        return selection;
    }

    // **一个后端只探它自己**（2026-11-01 删除 `auto` 之后）：
    // 写 `legacy_money` 就不看记分板，写 `scoreboard` 就不看 LegacyMoney。
    // 探不到 → 空实现，把原因交给调用方报错。账本换家必须由服主决定，
    // 不能因为「另一个后端刚好可用」就悄悄换一套（余额会一夜之间全变 0）。
    switch (configured) {
    case Backend::LegacyMoney:
        if (candidates.legacyMoney.available) {
            selection.chosen = Backend::LegacyMoney;
            return selection;
        }
        selection.skipped.emplace_back(Backend::LegacyMoney, candidates.legacyMoney.issue);
        break;
    case Backend::Scoreboard:
        if (candidates.scoreboard.available) {
            selection.chosen = Backend::Scoreboard;
            return selection;
        }
        selection.skipped.emplace_back(Backend::Scoreboard, candidates.scoreboard.issue);
        break;
    case Backend::Builtin:
    case Backend::Null:
        break; // 上面已经返回
    }

    selection.chosen = Backend::Null;
    return selection;
}

Text backendSelectionText(Backend configured, BackendSelection const& selection) {
    auto const configuredName = text::lit(std::string{backendName(configured)});
    auto const chosenName     = text::lit(std::string{backendName(selection.chosen)});

    if (selection.skipped.empty()) {
        return text::keyed("shop.economy.selection", Tone::Default, {configuredName, chosenName});
    }
    // `selectBackend` 只会探配置里点名的那一个后端，所以跳过记录最多一条。
    // 真出现多条就退回不带细节的说法，而不是拼一串没有分隔符的词。
    if (selection.skipped.size() == 1) {
        auto const& [backend, issue] = selection.skipped.front();
        return text::keyed("shop.economy.selection.skipped", Tone::Default, {
            configuredName,
            chosenName,
            text::lit(std::string{backendName(backend)}),
            probeIssueText(issue),
        });
    }
    return text::keyed("shop.economy.selection", Tone::Default, {configuredName, chosenName});
}

bool ownerUnreachable(bool ownerOnline, bool backendSupportsOffline, bool requireOwnerOnline) {
    if (backendSupportsOffline) {
        return false; // 后端能给离线账户记账：店主在不在线与这一笔无关
    }
    return requireOwnerOnline && !ownerOnline;
}

std::optional<TransferResult> checkTransfer(
    EconomyProvider const& provider,
    Account const&         from,
    Account const&         to,
    std::int64_t           amount
) {
    if (amount <= 0) {
        return TransferResult{TransferStatus::Rejected, text::keyed("shop.economy.reject.amount_not_positive")};
    }
    if (from.empty() && to.empty()) {
        // 两边都是系统账户：既没有钱生出来，也没有钱被销毁，这笔转账没有意义。
        return TransferResult{TransferStatus::Rejected, text::keyed("shop.economy.reject.both_system")};
    }
    // 空付款方 = 从系统付款（凭空记账，管理员店的卖出方向，DESIGN §7.4）。
    if (from.empty() && !provider.supportsSystemAccount()) {
        return TransferResult{TransferStatus::Rejected, text::keyed("shop.economy.reject.no_system_payer")};
    }
    // `to` 为空是合法的（付给系统 = 销毁），与「自己转给自己」不是一回事。
    if (!to.empty() && from == to) {
        return TransferResult{TransferStatus::Rejected, text::keyed("shop.economy.reject.same_account")};
    }
    if (to.empty() && !provider.supportsMoneySink() && !provider.supportsSystemAccount()) {
        // 空账户 = 付给系统（销毁）。不声明支持的后端拿它没谱，当成普通账户会变成一笔读不到的入账。
        return TransferResult{TransferStatus::Rejected, text::keyed("shop.economy.reject.no_money_sink")};
    }
    return std::nullopt;
}

TransferResult
EconomyProvider::transfer(Account const& from, Account const& to, std::int64_t amount, bool allowNegative) {
    if (auto const invalid = checkTransfer(*this, from, to, amount)) {
        return *invalid;
    }
    if (from.empty()) {
        // 从系统付款 = 凭空记账（管理员店的卖出方向，DESIGN §7.4）。只有一笔动作，
        // 没有「扣了没入」的中间态，所以不需要补偿。
        if (add(to, amount)) {
            return {TransferStatus::Ok, {}};
        }
        return {TransferStatus::Rejected, text::keyed("shop.economy.reject.system_payout_failed")};
    }
    if (to.empty()) {
        // 付给系统 = 销毁（管理员店的买入方向、商店税）。同样只有一笔动作。
        if (reduce(from, amount, allowNegative)) {
            return {TransferStatus::Ok, {}};
        }
        return {TransferStatus::Rejected, text::keyed("shop.economy.reject.payer_reduce_failed")};
    }
    if (!reduce(from, amount, allowNegative)) {
        return {TransferStatus::Rejected, text::keyed("shop.economy.reject.payer_reduce_failed")};
    }
    if (add(to, amount)) {
        return {TransferStatus::Ok, {}};
    }

    // 钱已经从付款方身上扣掉了：必须加回去，否则就是凭空消失。
    if (add(from, amount)) {
        return {TransferStatus::Rejected, text::keyed("shop.economy.reject.receiver_failed_refunded")};
    }
    return {TransferStatus::Broken, text::keyed("shop.economy.reject.receiver_failed_refund_failed")};
}

Account shopSideAccount(bool adminShop, std::string_view ownerXuid) {
    if (adminShop) {
        return Account{}; // 系统账户（空键）：管理员店的资金不归任何玩家
    }
    return Account{ownerXuid};
}

std::int64_t shopSideFunds(bool adminShop, std::int64_t ownerFunds) {
    if (adminShop) {
        return kUnlimitedFunds;
    }
    return ownerFunds;
}

std::optional<int> toScoreValue(std::int64_t amount) {
    if (amount < std::numeric_limits<int>::min() || amount > std::numeric_limits<int>::max()) {
        return std::nullopt;
    }
    return static_cast<int>(amount);
}

} // namespace ss::shop::economy
