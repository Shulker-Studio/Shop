#include "shop/Economy.h"

#include <doctest/doctest.h>
#include <limits>
#include <map>
#include <set>
#include <string>

namespace {

using namespace ss::shop::economy;

/// 内存账本：用来测 `transfer` 的默认实现（含补偿），不需要任何引擎。
class FakeEconomy final : public EconomyProvider {
public:
    std::string_view name() const override { return "fake"; }
    bool             supportsOffline() const override { return true; }

    std::optional<std::int64_t> get(Account const& account) const override {
        auto it = mBalances.find(account);
        if (it == mBalances.end()) {
            /// 从未上账的账户：后端把它当 0 元；只有登记为 unreadable 的才真的读不到。
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
            // 模拟「退款也失败」：扣款成功后付款方也收不了钱了。
            mRefuseAccounts.insert(account);
        }
        return ok;
    }

    /// 一起拒掉的账户：模拟「收不了钱」的后端（例如离线玩家的记分板）。
    void refuse(Account const& account) { mRefuseAccounts.insert(account); }

    /// 读不到余额的账户：模拟「账户不可记账」而不是「余额 0」。
    void unreadable(Account const& account) { mUnreadable.insert(account); }

    bool refusePayerAfterReduce = false;

private:
    std::map<Account, std::int64_t> mBalances;
    std::set<Account>               mRefuseAccounts;
    std::set<Account>               mUnreadable;
};

} // namespace

TEST_CASE("记分板分值：int32 范围检查不截断") {
    CHECK(toScoreValue(0) == std::optional<int>{0});
    CHECK(toScoreValue(-5) == std::optional<int>{-5});
    CHECK(toScoreValue(std::numeric_limits<int>::max()) == std::optional<int>{std::numeric_limits<int>::max()});
    CHECK(toScoreValue(std::numeric_limits<int>::min()) == std::optional<int>{std::numeric_limits<int>::min()});

    CHECK_FALSE(toScoreValue(std::int64_t{std::numeric_limits<int>::max()} + 1).has_value());
    CHECK_FALSE(toScoreValue(std::int64_t{std::numeric_limits<int>::min()} - 1).has_value());
    CHECK_FALSE(toScoreValue(std::numeric_limits<std::int64_t>::max()).has_value());
    CHECK_FALSE(toScoreValue(std::numeric_limits<std::int64_t>::min()).has_value());
}

TEST_CASE("空实现：谁的余额都读不到、写都成功、但不算可用后端") {
    NullEconomy backend;
    CHECK_FALSE(backend.get("any").has_value());
    CHECK(backend.add("any", 100));
    CHECK_FALSE(backend.get("any").has_value());
    CHECK_FALSE(backend.available());
    CHECK_FALSE(backend.supportsOffline());
}

TEST_CASE("余额：读不到与「余额为 0」是两件事") {
    FakeEconomy backend;
    backend.set("rich", 0);
    backend.unreadable("offline");

    // 上过账、余额就是 0：读得到，值是 0
    REQUIRE(backend.get("rich").has_value());
    CHECK(*backend.get("rich") == 0);
    // 从未上账：这个后端把它当 0 元
    REQUIRE(backend.get("new").has_value());
    CHECK(*backend.get("new") == 0);
    // 账户不可记账：读不到，调用方不能当 0 处理（DESIGN §8.2）
    CHECK_FALSE(backend.get("offline").has_value());
}

TEST_CASE("转账：双方余额都按金额变动") {
    FakeEconomy backend;
    backend.set("buyer", 100);
    backend.set("owner", 5);

    auto const result = backend.transfer("buyer", "owner", 30);
    REQUIRE(result.ok());
    CHECK(backend.get("buyer") == 70);
    CHECK(backend.get("owner") == 35);
}

TEST_CASE("转账：余额不足时双方都不动") {
    FakeEconomy backend;
    backend.set("buyer", 20);
    backend.set("owner", 5);

    auto const result = backend.transfer("buyer", "owner", 30);
    CHECK(result.status == TransferStatus::Rejected);
    CHECK_FALSE(result.broken());
    CHECK(backend.get("buyer") == 20);
    CHECK(backend.get("owner") == 5);
}

TEST_CASE("转账：金额非法或同账户时拒绝，且不动账") {
    FakeEconomy backend;
    backend.set("buyer", 100);

    CHECK(backend.transfer("buyer", "owner", 0).status == TransferStatus::Rejected);
    CHECK(backend.transfer("buyer", "owner", -10).status == TransferStatus::Rejected);
    CHECK(backend.transfer("buyer", "buyer", 10).status == TransferStatus::Rejected);
    CHECK(backend.get("buyer") == 100);
}

TEST_CASE("转账：收款方收不了钱时把钱退回付款方") {
    FakeEconomy backend;
    backend.set("buyer", 100);
    backend.refuse("owner");

    auto const result = backend.transfer("buyer", "owner", 30);
    CHECK(result.status == TransferStatus::Rejected);
    CHECK_FALSE(result.broken());
    CHECK(backend.get("buyer") == 100);
    CHECK(backend.get("owner") == 0);
}

TEST_CASE("转账：退款也失败时报告 Broken，不当普通失败处理") {
    FakeEconomy backend;
    backend.refusePayerAfterReduce = true;
    backend.set("buyer", 100);
    backend.refuse("owner");

    auto const result = backend.transfer("buyer", "owner", 30);
    CHECK(result.status == TransferStatus::Broken);
    CHECK(result.broken());
}

TEST_CASE("后端名与枚举一一对应：配置里写的名字只有一张表") {
    for (auto backend : {
             Backend::Auto,
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
}

TEST_CASE("后端名清单文本用于错配置提示") {
    auto const text = backendNamesText(" / ");
    CHECK(text == "auto / legacy_money / scoreboard / builtin / null");
}
