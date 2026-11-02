#include "shop/Economy.h"

#include "../TextHelper.h"
#include "shop/TradePricing.h"

#include <doctest/doctest.h>
#include <limits>
#include <map>
#include <set>
#include <string>

namespace {

using namespace ss::shop::economy;

/// 内存账本： 用来测 `transfer` 的默认实现（含补偿），不需要任何引擎。
class FakeEconomy final : public EconomyProvider {
public:
    std::string_view name() const override { return "fake"; }
    bool             supportsOffline() const override { return true; }
    bool             supportsMoneySink() const override { return moneySink; }
    bool             supportsSystemAccount() const override { return systemAccount; }

    /// 后端能力开关： 默认两者都没有（像一个既不收税也不能当系统账户的后端）。
    bool moneySink     = false;
    bool systemAccount = false;

    std::optional<std::int64_t> get(Account const& account) const override {
        auto it = mBalances.find(account);
        if (it == mBalances.end()) {
            /// 从未上账的账户： 后端把它当 0 元；只有登记为 unreadable 的才真的读不到。
            return mUnreadable.count(account) != 0 ? std::nullopt : std::optional<std::int64_t>{0};
        }
        return it->second;
    }
    bool set(Account const& account, std::int64_t amount) override {
        if (mRefuseAccounts.count(account) != 0) {
            return false;
        }
        mBalances[account] = amount;
        return true;
    }
    bool add(Account const& account, std::int64_t amount) override {
        if (amount <= 0) {
            return false;
        }
        auto const current = get(account);
        if (!current) {
            return false;
        }
        return set(account, *current + amount);
    }
    bool reduce(Account const& account, std::int64_t amount, bool allowNegative) override {
        if (amount <= 0) {
            return false;
        }
        auto const current = get(account);
        if (!current) {
            return false;
        }
        if (!allowNegative && *current < amount) {
            return false;
        }
        bool const ok = set(account, *current - amount);
        if (ok && refusePayerAfterReduce) {
            // 模拟「退款也失败」： 扣款成功后付款方也收不了钱了。
            mRefuseAccounts.insert(account);
        }
        return ok;
    }

    /// 一起拒掉的账户： 模拟「收不了钱」的后端（例如离线玩家的记分板）。
    void refuse(Account const& account) { mRefuseAccounts.insert(account); }

    /// 读不到余额的账户： 模拟「账户不可记账」而不是「余额 0」。
    void unreadable(Account const& account) { mUnreadable.insert(account); }

    bool refusePayerAfterReduce = false;

private:
    std::map<Account, std::int64_t> mBalances;
    std::set<Account>               mRefuseAccounts;
    std::set<Account>               mUnreadable;
};

} // namespace

TEST_CASE("记分板分值: int32 范围检查不截断") {
    CHECK(toScoreValue(0) == std::optional<int>{0});
    CHECK(toScoreValue(-5) == std::optional<int>{-5});
    CHECK(toScoreValue(std::numeric_limits<int>::max()) == std::optional<int>{std::numeric_limits<int>::max()});
    CHECK(toScoreValue(std::numeric_limits<int>::min()) == std::optional<int>{std::numeric_limits<int>::min()});

    CHECK_FALSE(toScoreValue(std::int64_t{std::numeric_limits<int>::max()} + 1).has_value());
    CHECK_FALSE(toScoreValue(std::int64_t{std::numeric_limits<int>::min()} - 1).has_value());
    CHECK_FALSE(toScoreValue(std::numeric_limits<std::int64_t>::max()).has_value());
    CHECK_FALSE(toScoreValue(std::numeric_limits<std::int64_t>::min()).has_value());
}

TEST_CASE("空实现: 谁的余额都读不到、写都成功、但不算可用后端") {
    NullEconomy backend;
    CHECK_FALSE(backend.get("any").has_value());
    CHECK(backend.add("any", 100));
    CHECK_FALSE(backend.get("any").has_value());
    CHECK_FALSE(backend.available());
    CHECK_FALSE(backend.supportsOffline());
}

TEST_CASE("余额: 读不到与「余额为 0」是两件事") {
    FakeEconomy backend;
    backend.set("rich", 0);
    backend.unreadable("offline");

    // 上过账、余额就是 0： 读得到，值是 0
    REQUIRE(backend.get("rich").has_value());
    CHECK(*backend.get("rich") == 0);
    // 从未上账： 这个后端把它当 0 元
    REQUIRE(backend.get("new").has_value());
    CHECK(*backend.get("new") == 0);
    // 账户不可记账： 读不到，调用方不能当 0 处理（DESIGN §8.2）
    CHECK_FALSE(backend.get("offline").has_value());
}

TEST_CASE("转账: 双方余额都按金额变动") {
    FakeEconomy backend;
    backend.set("buyer", 100);
    backend.set("owner", 5);

    auto const result = backend.transfer("buyer", "owner", 30);
    REQUIRE(result.ok());
    CHECK(backend.get("buyer") == 70);
    CHECK(backend.get("owner") == 35);
}

TEST_CASE("转账: 余额不足时双方都不动") {
    FakeEconomy backend;
    backend.set("buyer", 20);
    backend.set("owner", 5);

    auto const result = backend.transfer("buyer", "owner", 30);
    CHECK(result.status == TransferStatus::Rejected);
    CHECK_FALSE(result.broken());
    CHECK(backend.get("buyer") == 20);
    CHECK(backend.get("owner") == 5);
}

TEST_CASE("转账: 金额非法或同账户时拒绝，且不动账") {
    FakeEconomy backend;
    backend.set("buyer", 100);

    CHECK(backend.transfer("buyer", "owner", 0).status == TransferStatus::Rejected);
    CHECK(backend.transfer("buyer", "owner", -10).status == TransferStatus::Rejected);
    CHECK(backend.transfer("buyer", "buyer", 10).status == TransferStatus::Rejected);
    CHECK(backend.get("buyer") == 100);
}

TEST_CASE("转账: 收款方收不了钱时把钱退回付款方") {
    FakeEconomy backend;
    backend.set("buyer", 100);
    backend.refuse("owner");

    auto const result = backend.transfer("buyer", "owner", 30);
    CHECK(result.status == TransferStatus::Rejected);
    CHECK_FALSE(result.broken());
    CHECK(backend.get("buyer") == 100);
    CHECK(backend.get("owner") == 0);
}

TEST_CASE("转账: 退款也失败时报告 Broken，不当普通失败处理") {
    FakeEconomy backend;
    backend.refusePayerAfterReduce = true;
    backend.set("buyer", 100);
    backend.refuse("owner");

    auto const result = backend.transfer("buyer", "owner", 30);
    CHECK(result.status == TransferStatus::Broken);
    CHECK(result.broken());
}

TEST_CASE("后端名与枚举一一对应: 配置里写的名字只有一张表") {
    for (auto backend : {
             Backend::LegacyMoney,
             Backend::Scoreboard,
             Backend::Builtin,
             Backend::Null,
         }) {
        auto const name = backendName(backend);
        REQUIRE_FALSE(name.empty());
        CHECK(parseBackend(name) == std::optional<Backend>{backend});
    }

    CHECK_FALSE(parseBackend("").has_value());
    CHECK_FALSE(parseBackend("legacy-money").has_value());
    CHECK_FALSE(parseBackend("AUTO").has_value());
    // `auto` 曾经是默认值，现在被删掉了（它等于「探不到就偷偷换账本」）。
    // 配置校验拿不到枚举值，才能按「是不是这个废弃的名字」给出专门的迁移提示。
    CHECK_FALSE(parseBackend("auto").has_value());
}

TEST_CASE("后端名清单文本用于错配置提示") {
    auto const text = backendNamesText(" / ");
    CHECK(text == "legacy_money / scoreboard / builtin / null");
    // 配置校验只该列「现在能写」的取值： builtin 是 P1，写它会单独报错
    CHECK(legalBackendNamesText(" / ") == "legacy_money / scoreboard / null");
    // 删掉的 `auto` 不许再出现在任何提示里，否则服主会照着提示写一个非法值
    CHECK(text.find("auto") == std::string::npos);
}

namespace {

BackendProbe usable() { return BackendProbe{true, ProbeIssue::None}; }

BackendProbe missing(ProbeIssue issue) { return BackendProbe{false, issue}; }

} // namespace

TEST_CASE("写了哪个后端就只认哪个: 探不到也不许换成另一个账本") {
    // legacy_money 探不到、记分板恰好可用 —— 这是删掉 `auto` 的核心场景： 
    // 绝不能因为「记分板刚好在」就悄悄换一套账本，必须落到空实现并把原因交出去。
    auto selection = selectBackend(Backend::LegacyMoney, {missing(ProbeIssue::ModuleMissing), usable()});
    CHECK(selection.chosen == Backend::Null);
    REQUIRE(selection.skipped.size() == 1);
    CHECK(selection.skipped[0].first == Backend::LegacyMoney);
    CHECK(selection.skipped[0].second == ProbeIssue::ModuleMissing);

    // 反过来： 记分板目标没建，LegacyMoney 可用，也不许顶上去
    selection = selectBackend(Backend::Scoreboard, {usable(), missing(ProbeIssue::ObjectiveMissing)});
    CHECK(selection.chosen == Backend::Null);
    REQUIRE(selection.skipped.size() == 1);
    CHECK(selection.skipped[0].first == Backend::Scoreboard);
}

TEST_CASE("显式后端: 可用就选它，其余情况一律空实现") {
    auto selection = selectBackend(Backend::LegacyMoney, {usable(), usable()});
    CHECK(selection.chosen == Backend::LegacyMoney);
    CHECK(selection.skipped.empty());

    // scoreboard： 不看 LegacyMoney（服主不想要它记账）
    selection = selectBackend(Backend::Scoreboard, {usable(), usable()});
    CHECK(selection.chosen == Backend::Scoreboard);
    CHECK(selection.skipped.empty());

    selection = selectBackend(Backend::Scoreboard, {usable(), missing(ProbeIssue::ObjectiveMissing)});
    CHECK(selection.chosen == Backend::Null);
    REQUIRE(selection.skipped.size() == 1);
    CHECK(selection.skipped[0].first == Backend::Scoreboard);

    // null 不需要探测任何候选： 即使两个后端都可用也依旧选它
    selection = selectBackend(Backend::Null, {usable(), usable()});
    CHECK(selection.chosen == Backend::Null);
    CHECK(selection.skipped.empty());

    // builtin（P1）同理： 配置校验已经拒了它，漏到这儿也不许拿记分板顶替
    selection = selectBackend(Backend::Builtin, {usable(), usable()});
    CHECK(selection.chosen == Backend::Null);
    CHECK(selection.skipped.empty());
}

TEST_CASE("探测计划: 只探配置里写的那一个") {
    // 只探自己： 不再有「顺手也探一下别的后端」这回事（没有降级了，探了也没用）
    auto const legacyProbes = probesFor(Backend::LegacyMoney);
    CHECK(legacyProbes.legacyMoney);
    CHECK_FALSE(legacyProbes.scoreboard);

    // 显式 scoreboard 不去碰 LegacyMoney
    auto const scoreboardProbes = probesFor(Backend::Scoreboard);
    CHECK_FALSE(scoreboardProbes.legacyMoney);
    CHECK(scoreboardProbes.scoreboard);

    // 空实现与 builtin 谁都不探
    for (auto backend : {Backend::Null, Backend::Builtin}) {
        auto const probes = probesFor(backend);
        CHECK_FALSE(probes.legacyMoney);
        CHECK_FALSE(probes.scoreboard);
    }
}

TEST_CASE("探测结果文案: 说清选中了谁、为什么没用成") {
    CHECK(plainText(backendSelectionText(
              Backend::LegacyMoney,
              selectBackend(Backend::LegacyMoney, {usable(), usable()})
          ))
          == "配置 legacy_money → 选用 legacy_money");

    auto const text = plainText(backendSelectionText(
        Backend::LegacyMoney,
        selectBackend(Backend::LegacyMoney, {missing(ProbeIssue::ModuleMissing), usable()})
    ));
    CHECK(text.find("选用 null") != std::string::npos);
    CHECK(text.find("legacy_money") != std::string::npos);
    CHECK(text.find(plainText(probeIssueText(ProbeIssue::ModuleMissing))) != std::string::npos);

    // 每个失败原因都有说明，不能是空的（日志里会变成「跳过 x: 」）
    for (auto issue : {
             ProbeIssue::None,
             ProbeIssue::ModuleMissing,
             ProbeIssue::ExportsMissing,
             ProbeIssue::ObjectiveMissing,
             ProbeIssue::ObjectiveCreateFailed,
         }) {
        CHECK_FALSE(std::string{plainText(probeIssueText(issue))}.empty());
    }
}

TEST_CASE("店主离线判据: 支持离线的后端不看店主在不在线") {
    // 支持离线的后端（LegacyMoney / builtin）： 四种组合（在线与否 × 配置开关）全部不拦
    for (bool ownerOnline : {true, false}) {
        for (bool requireOwnerOnline : {true, false}) {
            CHECK_FALSE(ownerUnreachable(ownerOnline, /*backendSupportsOffline=*/true, requireOwnerOnline));
        }
    }

    // 记分板（不支持离线）： 默认（requireOwnerOnline）时店主不在线就拦，店主在线不拦
    CHECK(ownerUnreachable(/*ownerOnline=*/false, /*backendSupportsOffline=*/false, /*requireOwnerOnline=*/true));
    CHECK_FALSE(ownerUnreachable(true, false, true));

    // 关掉 requireOwnerOnline： 不再提前拦，让这一笔走到动钱那一步再失败
    CHECK_FALSE(ownerUnreachable(false, false, false));
    CHECK_FALSE(ownerUnreachable(true, false, false));
}

TEST_CASE("转账: 不支持系统账户的后端拒绝「付给空账户」") {
    FakeEconomy backend; // 默认 supportsMoneySink() == false，也没有系统账户
    backend.set("buyer", 100);

    auto const result = backend.transfer("buyer", "", 10);
    CHECK(result.status == TransferStatus::Rejected);
    CHECK_FALSE(result.broken());
    CHECK(backend.get("buyer") == 100);
}

TEST_CASE("系统账户: 生钱与灭钱是两个判据，管理员店靠后者之外的那一项") {
    // 1. 什么都不声明： 两个方向都拒，余额一分不动
    {
        FakeEconomy backend;
        backend.set("buyer", 100);
        CHECK(backend.transfer("buyer", "", 10).status == TransferStatus::Rejected);
        CHECK(backend.transfer("", "buyer", 10).status == TransferStatus::Rejected);
        CHECK(backend.get("buyer") == 100);
    }

    // 2. 只有税用的「销毁」能力（记分板式的组合）： 能付给系统，不能从系统付款
    {
        FakeEconomy backend;
        backend.moneySink = true;
        backend.set("buyer", 100);
        CHECK(backend.transfer("buyer", "", 10).ok());
        CHECK(backend.get("buyer") == 90);
        CHECK(backend.transfer("", "buyer", 10).status == TransferStatus::Rejected);
        CHECK(backend.get("buyer") == 90);
    }

    // 3. 管理员店的系统账户： 买入销毁、卖出凭空记账（DESIGN §7.4）
    {
        FakeEconomy backend;
        backend.systemAccount = true;
        backend.set("buyer", 100);
        CHECK(backend.transfer("buyer", "", 30).ok());
        CHECK(backend.get("buyer") == 70);
        CHECK(backend.transfer("", "buyer", 12).ok());
        CHECK(backend.get("buyer") == 82);
    }
}

TEST_CASE("管理员店的资金侧: 对手方是系统账户，余额不设上限") {
    // 普通店： 对手方就是牌上记录的店主，钱也就是他的
    CHECK(shopSideAccount(/*adminShop=*/false, "xuid-owner") == "xuid-owner");
    CHECK(shopSideFunds(/*adminShop=*/false, 25) == 25);

    // 管理员店： 没有玩家账户，资金侧不设上限（GLOSSARY）。哨兵有名字，读得懂也用得起。
    CHECK(shopSideAccount(/*adminShop=*/true, "xuid-owner").empty());
    CHECK(shopSideFunds(/*adminShop=*/true, 0) == kUnlimitedFunds);
    CHECK(shopSideFunds(/*adminShop=*/true, 25) == kUnlimitedFunds);
    // 拿去算份数不会溢出，也不会变成「不限制实例数」以外的东西
    CHECK(ss::shop::affordableUnits(10, kUnlimitedFunds) > 0);
}

TEST_CASE("转账参数校验只有一份: 金额、空的付款方 / 收款方、自转自") {
    FakeEconomy plain; // 没有销毁能力，也没有系统账户
    CHECK_FALSE(checkTransfer(plain, "a", "b", 10).has_value());

    CHECK(checkTransfer(plain, "a", "b", 0)->status == TransferStatus::Rejected);
    CHECK(checkTransfer(plain, "a", "b", -1)->status == TransferStatus::Rejected);
    CHECK(checkTransfer(plain, "a", "a", 10)->status == TransferStatus::Rejected);
    // 空付款方 = 从系统付款： 只有声明了系统账户的后端才放行
    CHECK(checkTransfer(plain, "", "b", 10)->status == TransferStatus::Rejected);

    // 空收款方 = 付给系统： 销毁能力与系统账户能力任一即可
    FakeEconomy sink;
    sink.moneySink = true;
    CHECK_FALSE(checkTransfer(sink, "a", "", 10).has_value());
    CHECK(checkTransfer(plain, "a", "", 10)->status == TransferStatus::Rejected);

    FakeEconomy system;
    system.systemAccount = true;
    CHECK_FALSE(checkTransfer(system, "", "b", 10).has_value());
    CHECK_FALSE(checkTransfer(system, "a", "", 10).has_value());

    // 自己转给自己仍然拦（空账户有自己的一套判据，不靠这一条）。
    CHECK(checkTransfer(system, "", "", 10)->status == TransferStatus::Rejected);
}
