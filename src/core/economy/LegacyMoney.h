#pragma once

#include "core/economy/Provider.h"

#include <memory>
#include <string>

namespace ss::shop {

/// LegacyMoney 经济后端（DESIGN §8.6 第 2 条 / §8.7）。
///
/// **绝不链接 LegacyMoney，也绝不 include 它的 `LLMoney.h`**：那个头里全是
/// `__declspec(dllimport)`，一引用就会在 `Shop.dll` 里留下导入项，没装 LegacyMoney 的服务器
/// 连加载都会失败——可选后端就没了。这里只用 `GetModuleHandleW` + `GetProcAddress`
/// 拿 C 导出（x64 下是未修饰名，如 `LLMoney_Get`），函数指针的定义只在 `.cpp` 里。
///
/// 接口对照（`core/economy/Provider.h`）：
/// * 键就是 XUID，与 LegacyMoney 的存储键一致，不做 UUID 反查（DESIGN §8.7 第 1 条）；
/// * `transfer` 只走一次 `LLMoney_Trans`（SQLite 事务，天然原子，且它自己会应用 `pay_tax`），
///   绝不自己拼 Reduce + Add（会漏税或重复扣税，见 §8.7 第 2 条）；
/// * `to` 为空 = 付给系统（销毁）：这条正是商店税的落点（§8.9）。
class LegacyMoney final : public economy::EconomyProvider {
public:
    /// 探测：模块在进程里 + 五个导出都拿得到。
    ///
    /// @param issue 拿不到时写明原因（`ModuleMissing` / `ExportsMissing`），供降级日志使用。
    /// @return 空指针表示不可用（不抛异常：本工程不开异常）。
    static std::shared_ptr<LegacyMoney> create(economy::ProbeIssue& issue);

    std::string_view name() const override { return "legacy_money"; }
    /// LegacyMoney 按 XUID 记账，与玩家在不在线无关（DESIGN §8.5）。
    bool supportsOffline() const override { return true; }
    /// `LLMoney_Trans(from, "", n)` 就是把 n 从 from 身上扣掉、谁都不加。
    bool supportsMoneySink() const override { return true; }
    /// 管理员店的资金侧（DESIGN §7.4）：钱进 = `LLMoney_Trans(to="")` 销毁，
    /// 钱出 = `LLMoney_Add` 凭空记账。两个方向都有单次调用就能做完的导出。
    bool supportsSystemAccount() const override { return true; }

    /// 余额。**读不到时返回空**：`LLMoney_Get` 在空 XUID 或数据库出错时返回 `-1`
    /// （见它自己的 `src/API.cpp`），这不是余额，而是「读不到」。
    ///
    /// 已知边界：从未上过账的 XUID 会被 LegacyMoney **插入一行 `def_money`** 再返回它，
    /// 所以「新账户」读到的是默认金额（通常 0），而不是「读不到」。这是 LegacyMoney 的
    /// 账户语义，不是我们的（DESIGN §8.2 的「空 = 读不到」在它这里只剩 `-1` 一种来源）。
    std::optional<std::int64_t> get(economy::Account const& account) const override;
    bool                        set(economy::Account const& account, std::int64_t amount) override;
    bool                        add(economy::Account const& account, std::int64_t amount) override;
    bool reduce(economy::Account const& account, std::int64_t amount, bool allowNegative = false) override;

    /// 一次 `LLMoney_Trans` 完成转账；`to` 为空表示销毁。
    ///
    /// 失败只有两种：普通的「没动账」（余额不足、被其他插件的 BeforeEvent 拦下、数据库出错），
    /// 以及「DLL 已经不在了」（调用前会重新确认模块还在，避免调用悬空的函数指针）。
    /// 两者都不会出现 `Broken`：单次 `Trans` 要么整体提交要么整体回滚，没有需要补偿的中间态。
    economy::TransferResult transfer(
        economy::Account const& from,
        economy::Account const& to,
        std::int64_t            amount,
        bool                    allowNegative = false
    ) override;

private:
    // 导入的函数签名（与 `LLMoney.h` 逐字对应，但我们不 include 它）。
    // `long long` 在 Windows x64 与 `std::int64_t` 同宽，`.cpp` 里有 `static_assert`。
    using GetFn    = long long (*)(std::string);
    using SetFn    = bool (*)(std::string, long long);
    using AddFn    = bool (*)(std::string, long long);
    using ReduceFn = bool (*)(std::string, long long);
    using TransFn  = bool (*)(std::string, std::string, long long, std::string const&);

    struct Exports {
        GetFn    get    = nullptr;
        SetFn    set    = nullptr;
        AddFn    add    = nullptr;
        ReduceFn reduce = nullptr;
        TransFn  trans  = nullptr;
    };

    explicit LegacyMoney(Exports exports);

    /// `allowNegative` 不支持：只在第一次被要求时记一条 debug（DESIGN §8.7 第 5 条）。
    void warnNegativeOnce() const;

    /// 每次调用前重新确认模块还在：DLL 被卸载后，缓存的函数指针就是悬空的。
    bool modulePresent() const;

    Exports      mExports;
    mutable bool mMissingWarned  = false; ///< 「DLL 没了」只报一次，别刷屏
    mutable bool mNegativeWarned = false; ///< `allowNegative` 不支持也只报一次

    // 这两个标志是 `mutable` 且没有加锁：本后端的调用全部发生在服务器主线程
    // （交易走 `onServerThread`，点牌走事件回调），与 `EconomyProvider` 的线程安全要求
    // 靠这一条前提兜着；真要在别的线程调，这两个标志最多重复打一次日志，不参与任何判断。
};

} // namespace ss::shop
