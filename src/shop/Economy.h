#pragma once

#include "shop/Text.h"

#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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
///
/// **没有 `auto`（2026-11-01 删除）**：它原来表示「LegacyMoney 探不到就用记分板」，
/// 等于让服主的账本可能在一夜之间换一套（余额看起来全变 0），而日志里只有一条容易被
/// 忽略的 info。这与「配置非法就拒绝加载」的既有原则直接冲突——账本换家比配置写错严重得多。
/// 现在后端必须**明确写出来**，探不到就报错不启用交易，由服主决定改成哪一个。
enum class Backend : std::uint8_t { LegacyMoney, Scoreboard, Builtin, Null };

/// config.json 里写的后端名 → 后端。认不出返回空（配置非法）。
std::optional<Backend> parseBackend(std::string_view name);

/// 后端 → config.json 里写的名字。
std::string_view backendName(Backend backend);

/// 全部后端名拼成一段文本，用 `separator` 隔开；只用于错误提示。
std::string backendNamesText(std::string_view separator);

/// **目前能写进配置**的后端名（不含 P1 的 `builtin`）。
///
/// 配置校验的错误提示必须只列合法取值：把 `builtin` 列进「只能是……」
/// 会让服主以为自己写错了别的，其实那个值本身还没实现。
std::string legalBackendNamesText(std::string_view separator);

/// 探测一个候选后端时的失败原因（DESIGN §8.4 第 4 条：每一级降级都要说清为什么）。
///
/// 只描述「探测阶段」就知道的事实：模块在不在、导出齐不齐、记分板目标建不建得出来。
/// 运行期的失败（余额不足、转账被拦）不是探测问题，走 `TransferResult`。
enum class ProbeIssue : std::uint8_t {
    None,
    ModuleMissing,         ///< 进程里没有 LegacyMoney.dll
    ExportsMissing,        ///< DLL 在，但 `LLMoney_*` 导出拿不全（多半不是 C 接口那一版）
    ObjectiveMissing,      ///< 记分板目标不存在，且配置不允许自动创建
    ObjectiveCreateFailed, ///< 允许自动创建，但引擎没建成
};

/// 一个候选后端的探测结论。
struct BackendProbe {
    bool       available = false;
    ProbeIssue issue     = ProbeIssue::None; ///< `available == false` 时的原因
};

/// 这个配置需要探测哪几个候选（DESIGN §8.4）。
///
/// **每个后端只探它自己那一个**：写 `legacy_money` 就只问 LegacyMoney，写 `scoreboard`
/// 就只问记分板。一个都不探的只有 `null` 与 `builtin`（P1）。
struct BackendProbes {
    bool legacyMoney = false;
    bool scoreboard  = false;
};

/// 只有写哪个探哪个（见上）：不存在「顺手也探一下别的」的情况。
BackendProbes probesFor(Backend configured);

/// 探测阶段能问出来的全部事实。`NullEconomy` 永远可用，所以不在里面。
struct BackendCandidates {
    BackendProbe legacyMoney;
    BackendProbe scoreboard;
};

/// 选后端的结论。
struct BackendSelection {
    Backend chosen = Backend::Null;
    /// 选中之前被跳过的候选（按探测顺序），每项带失败原因。
    std::vector<std::pair<Backend, ProbeIssue>> skipped;
};

/// 按配置与探测结果选定后端（DESIGN §8.4）。纯函数：不碰世界、不打日志、不建目标。
///
/// **不做任何隐式降级（2026-11-01）**：写 `legacy_money` 探不到就是 `null`（余额读不到、
/// 交易全失败），不会悄悄改用记分板；写 `scoreboard` 目标建不出来同理。`skipped` 里带着
/// 失败原因，调用方据此报错并**在文案里告诉服主可以改成哪个值**——账本换家必须由人决定。
///
/// `builtin` 到不了这里（配置校验就拒了），万一到了按空实现处理。
BackendSelection selectBackend(Backend configured, BackendCandidates const& candidates);

/// 选中结果的说明（进日志与状态输出）。
///
/// 形如 `配置 legacy_money → 选用 legacy_money`，或探不到时的
/// `配置 scoreboard → 选用 null（跳过 scoreboard: 记分板目标不存在…）`。
Text backendSelectionText(Backend configured, BackendSelection const& selection);

/// 探测失败原因 → 玩家/服主可读的说明。
Text probeIssueText(ProbeIssue issue);

/// 店主不在线时，这一笔还能不能做（DESIGN §8.5）。
///
/// 判据是**后端能力**而不是「店主此刻在不在线」：支持离线的后端根本不看这两个入参。
/// `requireOwnerOnline` 只对不支持离线的后端有意义——它是服主的选择：默认提前拒绝，
/// 关掉就让这一笔走到动钱那一步再失败（`get` 读不到离线账户）。**开关不会凭空造出能力。**
/// （2026-11-01 起两个后端都支持离线，这个开关只留给将来新加的后端。）
bool ownerUnreachable(bool ownerOnline, bool backendSupportsOffline, bool requireOwnerOnline);

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
    Text           detail; ///< 失败原因：进日志、进玩家消息、进 OP 告警

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

    /// 这个后端算不算「真的能记账」。空实现不算：它谁的余额都读不到，任何成交都会变成白送。
    virtual bool available() const { return true; }

    /// 能否为离线玩家记账（DESIGN §8.5）。
    ///
    /// 两个后端现在都支持：LegacyMoney 按 XUID 记账；记分板借「身份按名字存」这一点，
    /// 用玩家表把 XUID 换成名字再写分（LSE 的 `mc.addPlayerScore(uuid, …)` 同一条路）。
    virtual bool supportsOffline() const = 0;

    /// 能不能把一笔钱付给「系统」——账户键传空串（DESIGN §8.9 的税被销毁）。
    ///
    /// LegacyMoney 的 `LLMoney_Trans(from, "", n)` 就是「从 from 扣掉 n，谁都不加」；
    /// 记分板没有这种账户，所以返回 false，调用方据此**整笔税都不算**（不是少记一条流水：
    /// 硬收会让店主少收、买家少付，而没有任何地方记着那笔钱）。
    ///
    /// 与 `supportsSystemAccount()` 分开：这一项只回答「税那一笔能不能销毁」，是商店税的政策
    /// （10 落地时记分板被刻意排除在外）；管理员店的资金端是另一项能力，判据与文案都不同，
    /// 别把两者合并——合并会让记分板突然开始收税。
    virtual bool supportsMoneySink() const { return false; }

    /// 能不能把「系统账户」当交易对手（DESIGN §7.4 的管理员店资金端）。
    ///
    /// 管理员店的资金侧不设上限（GLOSSARY），P0 的落地方式就是这一对动作：
    /// 玩家买入时钱被销毁（付给系统），玩家卖出时钱凭空记到玩家账上（从系统付款）。
    /// 声明了它就必须能走通**两个方向**：买入失败要把钱从系统退回玩家（凭空记账），
    /// 卖出失败要把钱从玩家退给系统（销毁）——缺一个方向，补偿路径就断了。
    ///
    /// LegacyMoney 用 `LLMoney_Add` / `LLMoney_Trans(to="")`；记分板用 `add` / `reduce`
    /// （只能动在线玩家，而交易的两头总有一头是玩家，够了）。
    virtual bool supportsSystemAccount() const { return false; }

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
    ///
    /// 系统账户的两条边（DESIGN §7.4）：
    /// * `to` 为空 = 付给系统（销毁）→ 直接 `reduce` 付款方；
    /// * `from` 为空 = 从系统付款（凭空记账）→ 直接 `add` 收款方。
    /// 两条都要先在 `checkTransfer` 里过能力声明；没有单边失败后的补偿问题
    /// （只有一笔动作，要么成要么没动）。
    virtual TransferResult
    transfer(Account const& from, Account const& to, std::int64_t amount, bool allowNegative = false);
};

/// 转账入参的公共校验（默认实现与各后端实现共用）。
///
/// 返回空 = 参数合法、可以往下走；否则是现成的拒绝结果。放在这里的原因很实际：
/// 金额非正、空的付款方 / 收款方、自己转自己、以及「这个后端能不能用系统账户」这几条错误，
/// 基类默认实现与每个覆写都要判一遍，文案只该有一份。
///
/// 系统账户（GLOSSARY）的两个方向在这里分开授权（DESIGN §7.4 / §8.9）：空收款方 = 把钱销毁，
/// `supportsMoneySink()` 或 `supportsSystemAccount()` 任一为真即可；空付款方 = 凭空记账，
/// 只有 `supportsSystemAccount()` 放行。
std::optional<TransferResult> checkTransfer(
    EconomyProvider const& provider,
    Account const&         from,
    Account const&         to,
    std::int64_t           amount
);

/// 「资金侧不设上限」的哨兵（GLOSSARY 的管理员店）。
///
/// 不用 `-1`：这个值会直接拿去算份数（`affordableUnits` 的除法），做过 `INT64_MAX` 后
/// 再被 `maxShopAmount` 与货量夹住，不会溢出；用负数就会在每个调用点都要先判哨兵。
inline constexpr std::int64_t kUnlimitedFunds = std::numeric_limits<std::int64_t>::max();

/// 一次交易里「店那一侧」的资金账户（DESIGN §7.4 / §11）。
///
/// 普通商店是店主 XUID；管理员商店是**系统账户**（空账户键）——管理权只给 OP，
/// 但资金不归任何玩家，所以不能拿牌上记录的创建者冒充收款方。
Account shopSideAccount(bool adminShop, std::string_view ownerXuid);

/// 店那一侧能付多少钱（卖出方向上「付款方余额」这一条上限的来源）。
///
/// 管理员店的资金侧不设上限（GLOSSARY），返回 `kUnlimitedFunds`：份数上限只剩「你手里的货」
/// 与「商店箱还能装几份」两条，不会出现「系统账户的钱不够」这种不可能发生的原因。
std::int64_t shopSideFunds(bool adminShop, std::int64_t ownerFunds);

/// 空实现：谁的余额都读不到，写操作一律「成功」但什么也不记（DESIGN §8.6 第 3 条）。
///
/// 只用于开发与「一个经济后端都没有」的降级。「读不到」意味着表单不编造数字、
/// 交易必然失败，这是对的：没有经济后端就不该成交，而不是白送。
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
