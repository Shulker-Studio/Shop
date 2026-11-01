#pragma once

#include "shop/ContainerFinder.h"
#include "shop/TradePlan.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace ss::shop {

/// 流水三态（`p1-transaction-log` spec 的 `result` 列）。
///
/// 只收**终态**：`NeedsConfirm` 不是终态（玩家可能直接关掉确认表单），所以这里没有它的位置。
/// 名字与 mod 层的 `TradeStatus` 故意分开：那个枚举多了 `NeedsConfirm`，而
/// 「`Done` → `ok` / `Rejected` → `fail` / `Broken` → `frozen`」这条映射是流水一侧的知识，
/// 写在 `finishTrade`（mod）里。纯逻辑层只认这三个词，不反过来依赖 `src/mod`。
enum class TradeLogResult : std::uint8_t { Ok, Fail, Frozen };

/// 三态 → 文件里那三个词：`ok` / `fail` / `frozen`。
std::string_view tradeLogResultName(TradeLogResult result);

/// 拒绝原因 → **机器可读短名**（`shop_closed` / `no_money` / `no_stock` / `no_space` / …）。
///
/// spec 明确要求 `reason` 写这个而不是中文文案：文案会变、会被玩家引用、也会被改写，
/// 而短名是查询键（`jq 'select(.reason=="no_money")'`），历史流水不能跟着文案变。
/// 每加一个 `TradeRejection` 值都要同步加到这里（spec 明令）。`None` 不是拒绝，返回空串。
std::string_view tradeRejectionReasonName(TradeRejection rejection);

/// 一条成交流水（`p1-transaction-log` spec 的字段表）。
///
/// 一行一个 JSON 对象（NDJSON）：键序固定、`\n` 结尾、UTF-8 无 BOM、中文不转义。
/// 键序是给人眼和 `jq` 读的契约——调试时「第 6 个字段是方向」比「先 `jq keys`」省事。
///
/// **不记录余额快照**（会泄露他人财力，也会让日志随退款重写而失真），也**不记录容器坐标**
/// （能从牌坐标推导，留着日后加字段的空间）。
struct TradeRecord {
    /// 商店牌坐标。`dimensionId` 与 `x/y/z` 一起放进 `shop{dim,x,y,z}`。
    struct ShopCoord {
        int dimensionId = 0;
        int x           = 0;
        int y           = 0;
        int z           = 0;
    };

    /// 一个玩家（店主 / 交易者）。XUID 是身份，名字只是「当时叫什么」。
    struct Party {
        std::string xuid;
        std::string name;
    };

    /// 成交的商品。`id` 是 `ItemStack::getTypeName()`、`name` 是当时的展示名。
    /// **没有 `nbt`**：完整 SNBT 可能几十 KB，塞进业务流水会把一行撑成几十 KB，
    /// 被销毁物品的 NBT 走 `DiscardRecord` 那个单独的文件（spec D5）。
    struct Item {
        std::string id;
        int         aux = 0;
        std::string name;
    };

    std::string    ts; ///< 服务器本地时间，ISO-8601 带时区偏移
    TradeLogResult result = TradeLogResult::Ok;
    /// `result != ok` 时的机器可读原因（`no_money` / `no_stock` / …），**不是中文文案**。
    /// `ok` 时为空串，而序列化时**直接省掉这个键**（spec：`ok` 不带 `reason`）——
    /// `jq 'has("reason")'` 因此就能分出成交与失败的行。
    std::string    reason;
    TradeDirection dir = TradeDirection::Buy; ///< 从**玩家**视角：`buy` / `sell`
    ShopCoord      shop;
    Party          owner; ///< 管理员店：`xuid` 空串、`name` 是 `adminShopName`
    bool           admin = false;
    std::string    backend; ///< 当时生效的经济后端名（`legacy_money` / `scoreboard` / `null`）
    Party          player;
    Item           item;
    int            units     = 0; ///< 成交份数
    int            items     = 0; ///< 总件数 = units × 每份件数
    std::int64_t   unitPrice = 0; ///< 一份的牌面价
    /// **主交易金额**：钱真正在玩家与店那一侧之间移动的那一笔（`Payment::main`）。
    /// 买入方向带商店税时它是 `total - tax`（税是另一笔付给系统账户的，由店主承担的经济效果
    /// 落在店主实收上）；卖出方向就是 `total`。所以它**不是** `total` 的别名。
    std::int64_t gross = 0;
    std::int64_t tax   = 0; ///< 本笔商店税；管理员店恒为 0
    /// 玩家实际付出（买入）/ 收到（卖出）的总额。与 `gross` 的差别见上。
    std::int64_t total = 0;
};

/// 交易事实：流水里「谁、在哪家店、买了什么、多少钱一份」那一半。
///
/// 与 `TradeRecord` 分开的原因是**什么时候知道它们**：这几项在 `buyUnits` / `sellUnits`
/// 拿到商店快照时就定了，而份数、金额、结论要等交易真的走完。拆开之后，
/// 「记录在 `Done` 之前造好」这条要求就落在一个不可能读到过期世界状态的位置上。
///
/// 份数与金额**不在这里**：唯一真相是 `TradePlan`，写流水时从它抄一次，不维护第二份。
struct TradeFacts {
    TradeDirection dir         = TradeDirection::Buy;
    int            dimensionId = 0;
    Cell           shop; ///< 商店牌坐标（不是容器坐标）
    /// 牌上记的店主 XUID。管理员店照原样传进来即可——`composeTradeRecord` 会清掉它。
    std::string  ownerXuid;
    std::string  ownerName; ///< 管理员店是 `adminShopName`
    bool         admin = false;
    std::string  backend;
    std::string  playerXuid;
    std::string  playerName;
    std::string  itemId;
    int          itemAux = 0;
    std::string  itemName;
    std::int64_t unitPrice = 0;
    /// 主交易金额（`Payment::main`）。只有真的动了钱才有意义；退款/拒绝的路径保持 0。
    std::int64_t gross = 0;
};

/// 把「事实 + 成交方案 + 结论」拼成一条完整的流水。
///
/// `ts` 由调用方传入：纯逻辑层不读时钟（读时钟的地方一处就够，也省得单测跟系统时间较劲）。
///
/// `admin` 为真时这里**强制**把 `owner.xuid` 清空、`tax` 归零：管理员店的对手方是系统账户
/// （DESIGN §7.4），它没有可收钱的店主账户，也没有可抽的成交额。放这里而不是指望每个调用点
/// 都记得——一条写着「店主收到钱」的管理员店流水会让人去追一个不存在的账户。
TradeRecord composeTradeRecord(
    TradeFacts const& facts,
    TradePlan const&  plan,
    TradeLogResult    result,
    std::string_view  reason,
    std::string       ts
);

/// 一行 JSON（NDJSON：键序固定、末尾一个 `\n`），键序见 spec 的字段表：
/// `ts` / `event` / `result` / `reason` / `dir` / `shop{dim,x,y,z}` / `owner{xuid,name}` /
/// `admin` / `backend` / `player{xuid,name}` / `item{id,aux,name}` / `units` / `items` /
/// `unitPrice` / `gross` / `tax` / `total`。
///
/// 用 `nlohmann::ordered_json` 而不是拼字符串：转义、数字格式、键序都交给它，
/// 我们只负责把字段按顺序装进去。中文原样写（`ensure_ascii = false`）——
/// 转成 `\u94bb` 之后 `jq` 还是那个字，但人眼在文件里就找不到了。
std::string tradeRecordJson(TradeRecord const& record);

/// `transactions-YYYY-MM-DD.jsonl`。`date` 就是 `ts` 的前 10 个字符，两者因此永远同一天。
std::string transactionLogFileName(std::string_view date);

/// 落盘前的最后一道闸：返回第一条不合规的说明（空 = 合规）。
///
/// 三条组合规则（spec 的 Testing Decisions）：`ok` 不带 `reason`；`fail` / `frozen` 必须带；
/// `admin == true` 时 `tax == 0` 且 `owner.xuid` 为空串。另外 `ts` 不能为空——文件名要从它推出来。
///
/// 矛盾记录**不写**：一条 `result: "ok"` 还带 `reason` 的行会让服主以为这笔生意失败过，
/// 比少一行更误导。`composeTradeRecord` 已经把这几条做成造不出来的，
/// 所以这里报错只可能是有人手拼了记录（票 02 加入拒绝/冻结时会经过它）。
std::string_view tradeRecordProblem(TradeRecord const& record);

} // namespace ss::shop
