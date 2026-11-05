#pragma once

#include "core/trade/TradePlan.h"
#include "core/trade/UnitCount.h"
#include "util/i18n/Text.h"

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
/// 不含引擎类型，也不知道谁来点这块牌。文案对齐 DESIGN §10.1。
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
    UnitCount stockUnits;
    /// 玩家手里的同种商品够几份。卖出方向的限额来源（DESIGN §10.3），
    /// 与 `balance` 一样是「谁在看」的属性，由调用方填；信息表单不显示它。
    int heldUnits = 0;
    /// 商店箱还能装下几份商品（卖出方向的容量）。只有卖出表单显示它。
    /// 管理员店的模板箱收进来的货是销毁的（票 02），所以它是 `makeUnlimited()`：永远收得下。
    UnitCount boxRoomUnits;
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

/// 这个方向上「一份」的价格：买入看 `buyPrice`，卖出看 `sellPrice`（DESIGN §10.1）。
///
/// 表单正文、每档金额、确认正文、期望金额都要按方向选价，选择只写在这里。
std::optional<std::int64_t> unitPriceOf(ShopInfoData const& data, TradeDirection direction);

} // namespace ss::shop
