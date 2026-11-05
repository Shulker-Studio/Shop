#pragma once

#include "core/trade/UnitCount.h"
#include "util/i18n/Text.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace ss::shop {

/// `$ 10` / `金币 10` / 符号为空时只留 `10`。
std::string moneyText(std::string const& symbol, std::int64_t amount);

/// 份数文本：`3 份` / `无限（管理员店，永远有货）`。
///
/// 表单正文里凡是显示「库存 / 还剩几份」的地方都走它：一处写「无限」、一处写数字
/// 是最容易被读成「其实只剩几份」的两种真相（DESIGN §10.1 D2）。
Text stockUnitsText(UnitCount const& stock);

/// 收货方空间的文本：`7 份` / `无限（管理员店，永远收得下）`。
///
/// 与 `stockUnitsText` 共用同一个「有限写份数、无限不编数字」的实现，只有无限时那句括号里
/// 的解释不同——卖出方向的「无限」是**收得下**（货被销毁），不是「有货」。
Text boxRoomText(UnitCount const& stock);

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
/// 样品槽永远不出售（`core/container/SampleSlot.h` 的 `saleableSlotOrder`），所以「箱子里有商品」不再等于
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

/// 商品是**收纳袋**：内容存在服务端的动态容器注册表里，读不出来（`core/shop/BundleRules.h`）。
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

} // namespace ss::shop
