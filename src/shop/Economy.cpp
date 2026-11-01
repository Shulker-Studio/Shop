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
    case Backend::Auto:
    case Backend::LegacyMoney:
        return {true, true};
    case Backend::Scoreboard:
        return {false, true};
    case Backend::Builtin:
    case Backend::Null:
        break;
    }
    return {};
}

std::string probeIssueText(ProbeIssue issue) {
    switch (issue) {
    case ProbeIssue::ModuleMissing:
        return "进程里没有加载 LegacyMoney.dll";
    case ProbeIssue::ExportsMissing:
        return "LegacyMoney.dll 里拿不到 LLMoney_* 导出（多半不是 C 接口那一版）";
    case ProbeIssue::ObjectiveMissing:
        return "记分板目标不存在，且没有开启自动创建";
    case ProbeIssue::ObjectiveCreateFailed:
        return "记分板目标不存在，自动创建失败";
    case ProbeIssue::None:
        break;
    }
    return "可用";
}

BackendSelection selectBackend(Backend configured, BackendCandidates const& candidates) {
    BackendSelection selection;

    // 空实现不需要探测：「一个能记账的后端都没有」就是它的定义。
    // `builtin`（P1）同理：配置校验已经拒了它，万一漏到这儿也不拿记分板顶替——
    // 服主写了 builtin 就是不想要另外两个后端记账。
    if (configured == Backend::Null || configured == Backend::Builtin) {
        return selection;
    }

    // `auto` 与显式 `legacy_money` 走同一条候选链：服主点了名、但服务器上确实没有，
    // 能降级到记分板总比整服停摆好（DESIGN §8.4 第 4 条），原因会打进日志。
    // 显式 `scoreboard` 不从 legacy_money 开始：服主不想要 LegacyMoney 记账。
    bool const wantLegacy = configured == Backend::Auto || configured == Backend::LegacyMoney;

    if (wantLegacy) {
        if (candidates.legacyMoney.available) {
            selection.chosen = Backend::LegacyMoney;
            return selection;
        }
        selection.skipped.emplace_back(Backend::LegacyMoney, candidates.legacyMoney.issue);
    }

    if (candidates.scoreboard.available) {
        selection.chosen = Backend::Scoreboard;
        return selection;
    }
    selection.skipped.emplace_back(Backend::Scoreboard, candidates.scoreboard.issue);

    // 降级到底：空实现。`builtin`（P1）在上面就返回了，走不到这儿。
    selection.chosen = Backend::Null;
    return selection;
}

std::string backendSelectionText(Backend configured, BackendSelection const& selection) {
    std::string text = "配置 " + std::string{backendName(configured)} + " → 选用 "
                     + std::string{backendName(selection.chosen)};
    if (selection.skipped.empty()) {
        return text;
    }
    text += "（跳过 ";
    bool first = true;
    for (auto const& [backend, issue] : selection.skipped) {
        if (!first) {
            text += "；";
        }
        first = false;
        text += std::string{backendName(backend)} + "：" + probeIssueText(issue);
    }
    text += "）";
    return text;
}

bool ownerUnreachable(bool ownerOnline, bool backendSupportsOffline, bool requireOwnerOnline) {
    if (backendSupportsOffline) {
        return false; // 后端能给离线账户记账：店主在不在线与这一笔无关
    }
    return requireOwnerOnline && !ownerOnline;
}

std::optional<TransferResult>
checkTransfer(Account const& from, Account const& to, std::int64_t amount, bool supportsMoneySink) {
    if (amount <= 0) {
        return TransferResult{TransferStatus::Rejected, "转账金额必须大于 0"};
    }
    if (from.empty()) {
        return TransferResult{TransferStatus::Rejected, "付款账户为空，没法扣款"};
    }
    // `to` 为空是合法的（付给系统 = 销毁），与「自己转给自己」不是一回事。
    if (!to.empty() && from == to) {
        return TransferResult{TransferStatus::Rejected, "不能转给同一个账户"};
    }
    if (to.empty() && !supportsMoneySink) {
        // 空账户 = 付给系统（销毁）。不声明支持的后端拿它没谱，当成普通账户会变成一笔读不到的入账。
        return TransferResult{TransferStatus::Rejected, "这个后端没有可以把钱付给系统的账户"};
    }
    return std::nullopt;
}

TransferResult
EconomyProvider::transfer(Account const& from, Account const& to, std::int64_t amount, bool allowNegative) {
    if (auto const invalid = checkTransfer(from, to, amount, supportsMoneySink())) {
        return *invalid;
    }
    if (to.empty()) {
        // 声明支持「付给系统」却没覆写 `transfer`：基类的 `reduce` + `add` 拼不出销毁，
        // 硬走会变成一次「收款方不可记账、款已退回」的普通拒绝，读日志的人会被误导。
        return {TransferStatus::Rejected, "这个后端没有实现「付给系统」的转账"};
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
