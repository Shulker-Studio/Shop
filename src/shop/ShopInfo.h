#pragma once

#include "shop/SignProtection.h"
#include "shop/Text.h"
#include "shop/TradePlan.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ss::shop {

/// 容器类商品（潜影盒）盒里的一格，按格内内容去重合并后的结果。
///
/// 为什么要显示它：盒子商品的“一份”是**盒子里装的东西**，牌面第 4 行只写得出“潜影盒”三个字。
/// 买家掏钱前得知道这一盒里到底是什么、各有多少，否则只能凭信任。
struct BoxCell {
    std::string name;  ///< 展示名（`ItemStack::getName()`，游戏已按客户端语言本地化）
    int         count = 0;
};

/// 信息表单要显示的全部内容，已经解析与统计好。
///
/// 纯逻辑：不含引擎类型，也不知道谁来点这块牌。文案对齐 DESIGN §10.1。
struct ShopInfoData {
    std::string                 ownerName;         ///< 店主显示名；管理员商店可以留空
    bool                        adminShop = false; ///< 管理员商店（DESIGN §7.4）
    /// 停业中（牌面第 3 行的 `:x`，票 03）：谁都不能买卖，但牌子、归属、库存、保护全不变。
    /// 它与 `unavailableReason` **分开**：那个是「现在不可用的原因」（店主离线 / 没后端……），
    /// 这个是店主自己拉的闸，两种状态得能同时说清楚。
    bool                        closed = false;
    std::string                 itemName;          ///< 商品展示名；容器空时用牌面第 4 行
    /// 箱子里现在**有没有**商品（第一个非空槽位）。
    ///
    /// 与 `itemName` 必须分开：`itemName` 在箱子空时退回牌面第 4 行，所以它回答不了
    /// “商品还在不在”。“更新商品名”这个按钮就靠这一位决定给不给（没有商品时点了只是
    /// 把牌面原文抄回牌上）。
    bool                        hasProduct = false;
    /// 容器类商品（潜影盒）盒里装的东西，按格内内容去重合并；空 = 不是容器类商品或盒子是空的。
    std::vector<BoxCell>        boxContents;
    int                         amountPerUnit = 0; ///< 一份的件数（牌面第 2 行）
    std::optional<std::int64_t> buyPrice;          ///< 一份的买入价，玩家付给店主
    std::optional<std::int64_t> sellPrice;         ///< 一份的卖出价，店主付给玩家
    /// 可成交份数（商店箱里件数 / 每份件数）。管理员店的模板箱是 `makeUnlimited()`：
    /// 表单写「无限」，`planTrade` 的 `min` 里没有这一项（票 01）。
    StockView stockUnits;
    /// 玩家手里的同种商品够几份。卖出方向的限额来源（DESIGN §10.3），
    /// 与 `balance` 一样是「谁在看」的属性，由调用方填；信息表单不显示它。
    int heldUnits = 0;
    /// 商店箱还能装下几份商品（卖出方向的容量）。只有卖出表单显示它。
    /// 管理员店的模板箱收进来的货是销毁的（票 02），所以它是 `makeUnlimited()`：永远收得下。
    StockView boxRoomUnits;
    /// 店主的余额（卖出方向真正的限额来源）。空 = 读不到 / 这一笔是免费收。
    std::optional<std::int64_t> ownerBalance;
    /// 余额。空 = **读不到**（没有可用后端，或后端没给出这个账户的余额），
    /// 不是「余额为 0」——两者必须分开（DESIGN §8.2）。
    std::optional<std::int64_t> balance;
    /// 这家店现在**不能交易**的原因（店主收不了钱、或服务器没有可用经济后端）。
    /// 非空时不给买卖按钮，正文直接写这条原因，免得玩家点进去才被拒（DESIGN §8.5）。
    ///
    /// 与 `stockNote` 分开：这个是“一个按钮都不给”，那个只是解释一个数字。
    /// 把“只剩样品”塞进这里会连「店收购」一起关掉，而那个方向是好的。
    Text        unavailableReason;
    /// 箱子这一侧的补充说明：**不拦任何按钮**。目前只有一种：「只剩样品了」——
    /// 店主看到「可成交份数 0 份」时，箱子里明明还摆着一整堆商品，不解释一句会以为坏了。
    /// 店收购（玩家把货卖给店）在同一个箱子里照常能用，所以不能关按钮。
    Text        stockNote;
    /// 货币符号。只由调用方从 `economy.currencySymbol` 填进来，这里不留默认值：
    /// 默认值只能有一处定义，否则配置改了表单不跟着变。
    std::string currencySymbol;
};

/// `$ 10` / `金币 10` / 符号为空时只留 `10`。
std::string moneyText(std::string const& symbol, std::int64_t amount);

/// 份数文本：`3 份` / `无限（管理员店，永远有货）`。
///
/// 表单正文里凡是显示「库存 / 还剩几份」的地方都走它：一处写「无限」、一处写数字
/// 是最容易被读成「其实只剩几份」的两种真相（DESIGN §10.1 D2）。
Text stockUnitsText(StockView const& stock);

/// 收货方空间的文本：`7 份` / `无限（管理员店，永远收得下）`。
///
/// 与 `stockUnitsText` 共用同一个「有限写份数、无限不编数字」的实现，只有无限时那句括号里
/// 的解释不同——卖出方向的「无限」是**收得下**（货被销毁），不是「有货」。
Text boxRoomText(StockView const& stock);

/// 这个方向上「一份」的价格：买入看 `buyPrice`，卖出看 `sellPrice`（DESIGN §10.1）。
///
/// 表单正文、每档金额、确认正文、期望金额都要按方向选价，选择只写在这里。
std::optional<std::int64_t> unitPriceOf(ShopInfoData const& data, TradeDirection direction);

/// 往正文里追加一行：不是首行就先补一个换行。各处正文共用，别各自拼一遍。
void appendLine(std::string& content, std::string const& line);

/// 店主收不了钱的原因（DESIGN §8.5）。表单与交易提示共用这一句，别写成两种说法。
Text ownerOfflineReason();

/// 读不到店主余额的原因。
///
/// 「店主余额是 0」与「读不到店主余额」是两件事（DESIGN §8.2）：前者是正常的
/// 「钱不够」，后者是后端出问题或账户不可记账。混成一句会把后者误报成
/// 「店主资金不足」，玩家申诉时无处可查。
Text ownerFundsUnknownReason();

/// 读不到**玩家自己**的余额的原因。买入要扣钱、卖出要收钱，两边的说法一样：
/// 这不是「你没钱」，而是后端没给出这个账户的余额（DESIGN §8.2）。
Text playerFundsUnknownReason();

/// 没有可用经济后端的原因。
Text noEconomyReason();

/// 后端当不了系统账户的原因（DESIGN §7.4）。
///
/// 管理员店的资金侧是系统账户（买入销毁、卖出凭空记账），靠后端的能力声明
/// `supportsSystemAccount()`；声明不了的就不能开买卖按钮，也不能在提交阶段放行。
Text noSystemAccountReason();

/// 商店税没收到时的原因（DESIGN §8.9）。`detail` 是后端给的细节，前缀只写一次。
Text taxChargeFailureReason(Text const& detail);

/// 收商店税时钱已经动过且没能还原（§10.2 第 8 步的严重错误）。
Text taxBrokenReason(Text const& detail);

/// 普通店的商店箱空了的原因：没东西可卖，也看不出这家店收什么（商品权威是第一个非空槽位，DESIGN §5.3）。
/// 牌面第 4 行仍作商品名显示（那是旧记录），所以这里只讲「现在没法交易」。
Text emptyBoxReason();

/// 盒子空了时的不可交易原因。**两条原因不许合并**（票 03）：
///
/// - 普通店：`emptyBoxReason()`——「商店箱里没有货」，意思是**卖光了**，补货就恢复；
/// - 管理员店：模板箱里没有样品，意思是**这家店还没设商品**（不是缺货，它永远不会缺货）。
///   「卖光了」与「还没设商品」是两件事：前者等补货，后者只能由 OP 放一件样品，
///   报错报成前一种会让玩家以为过一个小时就自动有了。
///
/// 判据在 mod 层是同一句「第一个非空槽位不存在」（`productSlotOf`），所以这里按店的性质分叉，
/// 而不是让调用方各自拼一句文案。
Text unavailableReasonForEmptyBox(bool adminShop);

/// 商品还在箱子里，但**一件都不能卖**：剩下的全是样品槽那一份。
///
/// 样品槽永远不出售（`SampleSlot.h` 的 `saleableSlotOrder`），所以「箱子里有商品」不再等于
/// 「有货可卖」：店主只放了一份的店会看到这条原因，而不是「商店箱里没有货」。
/// 两者必须分开：“没货”= 等补货，“只剩样品”= 店主得再补一批货到样品槽以外。
Text onlySampleLeftReason();

/// 商品是容器类物品（潜影盒），但**盒内物品不整齐**：混装或每格数量不同。
///
/// 为什么拦：容器类商品的“同一种”包含盒内内容（`sameShopItem`），而“每格都和第一格一样”
/// 才能让店主和买家对“这一份到底是什么”有一个无需逐格检查的说法。不整齐的盒子不能当商品，
/// 也不排除是店主想用一盒杂货冒充一盒钻石——那句文案得说清楚是内容的问题，不是缺货。
Text mixedContainerContentsReason();

/// 商品被服主禁售（`bannedItems`）：要么它自己的 ID 命中，要么盒内嵌套的某一件命中。
///
/// 与“盒内不整齐”分开说：那个是“整理一下就能卖”，这个是“服主不许卖”——店主去整理盒子
/// 白忙一趟。两条都不谈缺货。管理员店永远不会走到这里（禁售不管管理员店）。
Text bannedItemReason();

/// 商品是**收纳袋**：内容存在服务端的动态容器注册表里，读不出来（`ProductRules.h`）。
///
/// 与「禁售」分开说：禁售是服主的政策（换一件就能开），收纳袋是能力限制（读不到内容就没法
/// 核对、也没法把内容算进商品身份），而且不分店的性质——管理员店也一样。
Text bundleProductReason();

/// 商店箱现在能不能交易（只看箱子这一侧的状态）。**取值顺序就是判定优先级**：
/// 混装 > 空箱 > 只剩样品。混装排最前，因为它是“商品定义本身”有问题，比缺不缺货更根本；
/// 空箱排第二，因为那连商品都没有，“只剩样品”就无从谈起。
enum class BoxStockState : std::uint8_t {
    Saleable,       ///< 有货可卖
    OnlySampleLeft, ///< 商品还在，只是都算样品那份
    MixedContents,  ///< 商品是容器类物品但盒内不整齐
    Empty,          ///< 箱子里没有商品
};

/// 箱子这一侧的状态。`hasSaleableItems` 是**扣掉样品槽之后**还有没有货
/// （不是“总共有几件”）。
constexpr BoxStockState boxStockStateOf(bool empty, bool mixedContents, bool hasSaleableItems) {
    if (mixedContents) {
        return BoxStockState::MixedContents;
    }
    if (empty) {
        return BoxStockState::Empty;
    }
    if (!hasSaleableItems) {
        return BoxStockState::OnlySampleLeft;
    }
    return BoxStockState::Saleable;
}

/// 与 `boxStockStateOf` 一一对应：只有**真的一个按钮都不能给**的状态才有原因。
///
/// `Saleable` 与 `OnlySampleLeft` 都返回空：后者箱子这一侧确实卖不出去了，但「店收购」
/// （玩家把货卖给店）是好的——把按钮全关掉等于把一个能用的方向也封了。它的说明走 `stockNoteForStockState`。
Text unavailableReasonForStockState(BoxStockState state, bool adminShop);

/// 箱子状态的**说明性**补句（不拦任何按钮）：目前只有「只剩样品了」。
/// 除 `OnlySampleLeft` 外都返回空。
Text stockNoteForStockState(BoxStockState state);

/// 表单标题：普通店 `<店主> 的商店`，管理员店 `管理员商店`（DESIGN §10.1）。
Text shopFormTitle(ShopInfoData const& data);

/// 表单正文的每一行：店主、商品、每份件数、买入价、卖出价、可成交份数、余额。
std::vector<Text> shopInfoLines(ShopInfoData const& data);

/// 表单正文：`shopInfoLines` 用换行符拼起来，行尾不留换行。
std::vector<Text> shopFormContent(ShopInfoData const& data);

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
    ManageRefreshItemName, ///< 管理表单：按箱子里第一格重写牌面第 4 行（商品名）
    ToggleClosed,    ///< 管理表单：停业 / 恢复营业（03）
};

struct ShopFormButton {
    Text           label;
    ShopFormAction action = ShopFormAction::Buy;
};

/// 按钮列表：管理入口只看 `canManage`，买卖按钮只看 `role`、`closed` 与 `unavailableReason`。
///
/// 两件事**故意分开**（spec D5）：
/// - `canManage` 为真时**总是**给管理入口，与 `closed` / `unavailableReason` 无关——否则管理员店一停业
///   就一个按钮都没有，永远无法恢复营业（管理员店的 OP 在交易上是 `Visitor`）；
/// - 买卖按钮仍然按 `role`、`closed` 与 `unavailableReason` 给：停业或店主收不了钱时一个买卖按钮都不给，
///   这不是「不行就不行」，而是提前把必然失败的路径藏起来（DESIGN §8.5）。
///
/// 店主（`Manager`）不跟自己交易：除了管理入口不给买卖按钮（DESIGN §10.1）。
///
/// 文案取自 DESIGN §10.1：`购买（1 份 N 个 / B 价）`、`出售（1 份 N 个 / S 价）`、`管理商店`。
std::vector<ShopFormButton> shopFormButtons(ShopInfoData const& data, ShopFormRole role, bool canManage);

} // namespace ss::shop
