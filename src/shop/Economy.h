#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace ss::shop::economy {

/// 账户键：玩家的 XUID。
///
/// DESIGN §8.1 的接口签名用的是 `mce::UUID`，这里换成 XUID，理由两条：
/// 1. 纯逻辑层不得包含 `mc/` 头（`xmake.lua` 的 `check_pure_logic_headers` 会拦），
///    引擎 UUID 类型进不来；
/// 2. 店主身份本来就是牌面记录的 XUID（ADR-0005），LegacyMoney 也以 XUID 记账（DESIGN §8.7），
///    用 UUID 反而要多一次 XUID → UUID 反查。
/// 账户键只在本层当不透明字符串用，比较与哈希就是字符串比较。
using Account = std::string;

/// 经济后端（DESIGN §8.4 第 1 条的候选清单）。
///
/// 名字与枚举的对应只写在 `Economy.cpp` 那张表里：配置校验、后端选择、日志文案
/// 都从它取名，免得三个地方各写一份字符串。
enum class Backend : std::uint8_t { Auto, LegacyMoney, Scoreboard, Builtin, Null };

/// config.json 里写的后端名 → 后端。认不出返回空（配置非法）。
std::optional<Backend> parseBackend(std::string_view name);

/// 后端 → config.json 里写的名字。
std::string_view backendName(Backend backend);

/// 全部后端名拼成一段文本，用 `separator` 隔开；只用于错误提示。
std::string backendNamesText(std::string_view separator);

/// 一次转账的结果。
///
/// DESIGN §8.1 写的是「补偿失败抛异常」，但本工程不开异常（`set_exceptions("none")`），
/// 所以把「严重」做成返回值：`Broken` 表示钱可能已经动过且没能还原，
/// 调用方必须记严重日志并冻结这次操作（DESIGN §10.2 第 8 步），不能重试。
enum class TransferStatus : std::uint8_t {
    Ok,
    Rejected, ///< 没动账：余额不足、金额非法、账户不可记账……双方余额都不变
    Broken,   ///< 动了但没还原：补偿失败，属于严重错误
};

struct TransferResult {
    TransferStatus status = TransferStatus::Rejected;
    std::string    detail; ///< 中文说明，直接进日志

    [[nodiscard]] bool ok() const { return status == TransferStatus::Ok; }
    [[nodiscard]] bool broken() const { return status == TransferStatus::Broken; }
};

/// 经济后端（DESIGN §8.1）。实现方必须线程安全：表单回调和主线程都可能读余额。
///
/// 语义（DESIGN §8.2）：`amount` 必须 > 0；`reduce` 在余额不足且 `allowNegative == false`
/// 时返回 false 且余额不变；金额一律 `int64`，后端自己负责在自己的存储范围里拒绝而不是截断。
///
/// 与 §8.1 的两处偏离：
/// * 键用 `Account`（XUID）而不是 `mce::UUID`，理由见上；
/// * 没有 `getSymbol()`：货币符号是商店的展示属性，只从 `economy.currencySymbol` 读
///   （DESIGN §8.7 第 6 条明确说它拿不到后端配置），后端再提供一个 getter 只会造出
///   第二个符号来源。
class EconomyProvider {
public:
    using Ptr = std::shared_ptr<EconomyProvider>;

    virtual ~EconomyProvider() = default;

    /// 后端名字，进日志（DESIGN §8.4 第 4 条）。
    virtual std::string_view name() const = 0;

    /// 这个后端算不算「真的能记账」。空实现不算：它永远读 0，任何成交都会变成白送。
    virtual bool available() const { return true; }

    /// 能否为离线玩家记账（DESIGN §8.5）。记分板只能写在线玩家的分。
    virtual bool supportsOffline() const = 0;

    /// 余额。**读不到时返回空，返回 0 就真的是 0 元**。
    ///
    /// 这两件事必须分开（DESIGN §8.2）：卖出方向上「店主真的没钱」与「读不到店主的余额」
    /// 是两种完全不同的结果，把后者当成 0 会对着一个余额充足的店主报「店主资金不足」，
    /// 玩家申诉时无处可查。账户不可读的典型情况：玩家不在线（记分板）、后端没有这个账户、
    /// 记分板目标不存在。
    virtual std::optional<std::int64_t> get(Account const& account) const                        = 0;
    virtual bool                        set(Account const& account, std::int64_t amount)         = 0;
    virtual bool                        add(Account const& account, std::int64_t amount)         = 0;
    virtual bool reduce(Account const& account, std::int64_t amount, bool allowNegative = false) = 0;

    /// 默认实现：`reduce` + `add`，`add` 失败就把钱加回去（DESIGN §8.1）。
    ///
    /// 补偿也失败时返回 `Broken`：钱未必还在 `from` 身上，调用方不能当普通失败处理。
    virtual TransferResult
    transfer(Account const& from, Account const& to, std::int64_t amount, bool allowNegative = false);
};

/// 空实现：读余额永远是 0，写操作一律「成功」但什么也不记（DESIGN §8.6 第 3 条）。
///
/// 只用于开发与「一个经济后端都没有」的降级。余额 0 意味着买入必然失败，
/// 这是对的：没有经济后端就不该成交，而不是白送。
class NullEconomy final : public EconomyProvider {
public:
    std::string_view name() const override { return "null"; }
    bool             available() const override { return false; }
    bool             supportsOffline() const override { return false; }
    /// 什么也不记，所以谁的余额都读不到（而不是「余额是 0」）。
    std::optional<std::int64_t> get(Account const&) const override { return std::nullopt; }
    bool                        set(Account const&, std::int64_t) override { return true; }
    bool                        add(Account const&, std::int64_t) override { return true; }
    bool                        reduce(Account const&, std::int64_t, bool) override { return true; }
};

/// 金额 → 记分板分值（`int32`）。超出范围返回空，**不截断**（DESIGN §8.2）。
///
/// 记分板后端的每次写入都要过这一关，所以范围检查只有这一处。
std::optional<int> toScoreValue(std::int64_t amount);

} // namespace ss::shop::economy
