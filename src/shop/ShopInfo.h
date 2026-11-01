#pragma once

#include "shop/SignProtection.h"
#include "shop/TradePlan.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ss::shop {

/// 信息表单要显示的全部内容，已经解析与统计好。
///
/// 纯逻辑：不含引擎类型，也不知道谁来点这块牌。文案对齐 DESIGN §10.1。
struct ShopInfoData {
    std::string                 ownerName;         ///< 店主显示名；管理员商店可以留空
    bool                        adminShop = false; ///< 管理员商店（DESIGN §7.4）
    std::string                 itemName;          ///< 商品展示名；容器空时用牌面第 4 行
    int                         amountPerUnit = 0; ///< 一份的件数（牌面第 2 行）
    std::optional<std::int64_t> buyPrice;          ///< 一份的买入价，玩家付给店主
    std::optional<std::int64_t> sellPrice;         ///< 一份的卖出价，店主付给玩家
    int                         stockUnits = 0;    ///< 可成交份数（商店箱里件数 / 每份件数）
    /// 玩家手里的同种商品够几份。卖出方向的限额来源（DESIGN §10.3），
    /// 与 `balance` 一样是「谁在看」的属性，由调用方填；信息表单不显示它。
    int heldUnits = 0;
    /// 商店箱还能装下几份商品（卖出方向的容量）。只有卖出表单显示它。
    int boxRoomUnits = 0;
    /// 店主的余额（卖出方向真正的限额来源）。空 = 读不到 / 这一笔是免费收。
    std::optional<std::int64_t> ownerBalance;
    /// 余额。空 = **读不到**（没有可用后端，或后端没给出这个账户的余额），
    /// 不是「余额为 0」——两者必须分开（DESIGN §8.2）。
    std::optional<std::int64_t> balance;
    /// 这家店现在不能交易的原因（店主收不了钱、或服务器没有可用经济后端）。
    /// 非空时不给买卖按钮，正文直接写这条原因，免得玩家点进去才被拒（DESIGN §8.5）。
    std::string unavailableReason;
    /// 货币符号。只由调用方从 `economy.currencySymbol` 填进来，这里不留默认值：
    /// 默认值只能有一处定义，否则配置改了表单不跟着变。
    std::string currencySymbol;
};

/// `$ 10` / `金币 10` / 符号为空时只留 `10`。
std::string moneyText(std::string const& symbol, std::int64_t amount);

/// 这个方向上「一份」的价格：买入看 `buyPrice`，卖出看 `sellPrice`（DESIGN §10.1）。
///
/// 表单正文、每档金额、确认正文、期望金额都要按方向选价，选择只写在这里。
std::optional<std::int64_t> unitPriceOf(ShopInfoData const& data, TradeDirection direction);

/// 往正文里追加一行：不是首行就先补一个换行。各处正文共用，别各自拼一遍。
void appendLine(std::string& content, std::string const& line);

/// 店主收不了钱的原因（DESIGN §8.5）。表单与交易提示共用这一句，别写成两种说法。
std::string ownerOfflineReason();

/// 读不到店主余额的原因。
///
/// 「店主余额是 0」与「读不到店主余额」是两件事（DESIGN §8.2）：前者是正常的
/// 「钱不够」，后者是后端出问题或账户不可记账。混成一句会把后者误报成
/// 「店主资金不足」，玩家申诉时无处可查。
std::string ownerFundsUnknownReason();

/// 读不到**玩家自己**的余额的原因。买入要扣钱、卖出要收钱，两边的说法一样：
/// 这不是「你没钱」，而是后端没给出这个账户的余额（DESIGN §8.2）。
std::string playerFundsUnknownReason();

/// 没有可用经济后端的原因。
std::string noEconomyReason();

/// 后端当不了系统账户的原因（DESIGN §7.4）。
///
/// 管理员店的资金侧是系统账户（买入销毁、卖出凭空记账），靠后端的能力声明
/// `supportsSystemAccount()`；声明不了的就不能开买卖按钮，也不能在提交阶段放行。
std::string noSystemAccountReason();

/// 商店税没收到时的原因（DESIGN §8.9）。`detail` 是后端给的细节，前缀只写一次。
std::string taxChargeFailureReason(std::string const& detail);

/// 收商店税时钱已经动过且没能还原（§10.2 第 8 步的严重错误）。
std::string taxBrokenReason(std::string const& detail);

/// 商店箱空了的原因：没东西可卖，也看不出这家店收什么（商品权威是第一个非空槽位，DESIGN §5.3）。
/// 牌面第 4 行仍作商品名显示（那是旧记录），所以这里只讲「现在没法交易」。
std::string emptyBoxReason();

/// 表单标题：普通店 `<店主> 的商店`，管理员店 `管理员商店`（DESIGN §10.1）。
std::string shopFormTitle(ShopInfoData const& data);

/// 表单正文的每一行：店主、商品、每份件数、买入价、卖出价、可成交份数、余额。
std::vector<std::string> shopInfoLines(ShopInfoData const& data);

/// 表单正文：`shopInfoLines` 用换行符拼起来，行尾不留换行。
std::string shopFormContent(ShopInfoData const& data);

/// 这个表单给谁看：游客/买家，还是店主/OP。
enum class ShopFormRole : std::uint8_t { Visitor, Manager };

/// 点牌时给哪种表单角色（DESIGN §10.1 / §11）。
///
/// 这里只决定**买卖这一侧**的视角（买家 vs 店主），**不**决定管理入口：
/// 管理入口用 `canManageShop(shopActorOf(...))` 单独判（见 `shopFormButtons` 的 `canManage`）。
///
/// 普通店：店主与 OP 不跟自己交易，其余人拿买卖按钮。
/// **管理员店一律是访客**：它的资金对手方是系统账户（§7.4），所以「店主」不是交易的一侧，
/// OP 在这里也能正常买卖（包括当初建它的那个 OP）。管理能力（改牌、开箱、拆箱）不看这个角色，
/// 判据仍然是 `canManageShop`。
ShopFormRole shopFormRoleOf(ShopActor actor, bool adminShop);

/// 按钮要做的事。
///
/// 每个动作一个值，**不靠按钮下标猜**：`SimpleForm` 回调给的是下标，下标随按钮顺序变，
/// 停业时「管理商店」可能排在第 0 位也可能不在。
enum class ShopFormAction : std::uint8_t {
    Buy,             ///< 弹买入份数表单
    Sell,            ///< 弹出份数表单
    Manage,          ///< 进管理表单
    ManageBuyPrice,  ///< 管理表单：改买入价
    ManageSellPrice, ///< 管理表单：改卖出价
    ManageAmount,    ///< 管理表单：改每份件数
    ToggleClosed,    ///< 管理表单：停业 / 恢复营业（03）
};

struct ShopFormButton {
    std::string    label;
    ShopFormAction action = ShopFormAction::Buy;
};

/// 按钮列表：管理入口只看 `canManage`，买卖按钮只看 `role` 与 `unavailableReason`。
///
/// 两件事**故意分开**（spec D5）：
/// - `canManage` 为真时**总是**给管理入口，与 `unavailableReason` 无关——否则管理员店一停业
///   就一个按钮都没有，永远无法恢复营业（管理员店的 OP 在交易上是 `Visitor`）；
/// - 买卖按钮仍然按 `role` 与 `unavailableReason` 给：店主收不了钱时一个买卖按钮都不给，
///   这不是「不行就不行」，而是提前把必然失败的路径藏起来（DESIGN §8.5）。
///
/// 店主（`Manager`）不跟自己交易：除了管理入口不给买卖按钮（DESIGN §10.1）。
///
/// 文案取自 DESIGN §10.1：`购买（1 份 N 个 / B 价）`、`出售（1 份 N 个 / S 价）`、`管理商店`。
std::vector<ShopFormButton> shopFormButtons(ShopInfoData const& data, ShopFormRole role, bool canManage);

} // namespace ss::shop
