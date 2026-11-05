#pragma once

#include "core/shop/ShopInfoData.h"
#include "form/FormModel.h"
#include "core/sign/ShopSign.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ss::shop {

/// 管理表单的正文（spec P1「管理表单」D4）。
///
/// 复用信息表单的正文（`shopFormContent`：店主 / 商品 / 每份 / 买卖价 / 可成交份数 / 余额），
/// **不重新算一遍库存**：管理表单与信息表单看到的是同一份 `ShopInfoData`（店主改价时才看得到
/// 「现在箱里还剩几份」「余额够不够付」）。
///
/// 上蜡的牌补一句：蜡只锁手写编辑，管理表单照常能改（`DESIGN §9.2`），
/// 不写这句店主会以为牌坏了。
std::vector<Text> shopManageFormContent(ShopInfoData const& data, bool waxed);

/// 管理表单的按钮：改买入价 / 改卖出价 / 改每份件数 / 停业（停业中换成恢复营业）。
///
/// 文案带当前值：点进去之前就该知道现在是几。停业按钮按 `data.closed` 二选一：
/// 两个同时挂上去会出现一对互相矛盾的按钮。
std::vector<ShopFormButton> shopManageFormButtons(ShopInfoData const& data);

/// 一次管理表单改动的结果。
struct ManageEdit {
    std::optional<ShopSpec> spec;  ///< 改完的商店定义；空 = 这次不能改
    Text                    error; ///< `spec` 为空时给玩家的原因

    [[nodiscard]] bool ok() const { return spec.has_value(); }
};

/// 一次管理改动的目标状态（spec「Testing Decisions」的那一条缝）：
/// 当前定义 + 新买入价 + 新卖出价 + 新件数 + 停业位 → 新的定义或原因。
///
/// 每个字段一个 `optional`：**有值 = 这次改这一格**，`nullopt` = 原样带过去。
/// 价格与件数是输入框原文（因为「留空」与「没填」是两回事），停业位是布尔。
struct ManageEditRequest {
    std::optional<std::string> buyPrice; ///< 买入价输入框原文；空串 = 这一侧不做生意
    std::optional<std::string> sellPrice;
    std::optional<std::string> amount;
    std::optional<bool>        closed;   ///< 停业位（切换由调用方读当前值后决定）
};

/// 管理表单四个动作的唯一入口：改一块商店牌的某一格（或几格），其余原样带过去。
///
/// 三条校验只在这里（`applyPrice` / `validatedEdit`）：输入非法、两侧都清空
/// （第 3 行会变空，`looksLikeShopSign` 为假 → 牌与箱子整体失去保护，spec D6）、
/// 卖出价高于买入价（问 `sellPriceHigherThanBuy`，与手写编辑同一条规则）。
/// 停业**不豁免**其中任何一条：停业靠 `:x` 标记表达，第 3 行仍然必须有价。
///
/// 输出只给 `ShopSpec`：牌面文本由调用方用 `formatShopSign` 拼（规范化只有那一处），
/// 第 1 行（店主名）与第 4 行（商品名）也不归这里管。
ManageEdit applyManageEdit(ShopSpec const& current, ManageEditRequest const& request, ShopSignConfig const& config);

/// 改买入价：`applyManageEdit` 的薄包装（`{.buyPrice = input}`）。
///
/// 输入只收**非负整数或空串**（空 = 这一侧不做生意）：方向由「哪个输入框」表达，
/// 所以牌面写法 `B 10`、`free`、`1.5K` 一律不收。整数必须整串解析完，溢出算非法——
/// 不截断、不静默当 0。
///
/// 单独留一个名字是因为管理表单的「一次改一项」都走同一个函数指针类型（`ManageChange::apply`）。
ManageEdit withBuyPrice(ShopSpec const& current, std::string_view input, ShopSignConfig const& config);

/// 改卖出价：与 `withBuyPrice` 同一套解析与校验，只是换一侧（留空 = 这一侧不收购）。
ManageEdit withSellPrice(ShopSpec const& current, std::string_view input, ShopSignConfig const& config);

/// 改每份件数：`applyManageEdit` 的薄包装（`{.amount = input}`）。
///
/// 件数的文法不在这里写第二遍：直接问 `parseAmountLine`（牌面第 2 行的权威解析器），
/// 所以「什么叫合法件数」与手写编辑只有一份。件数没有「留空 = 这一侧不做生意」这一档——
/// 空、非整数、小于 1、大于 `config.maxShopAmount` 一律按同一条范围提示拒绝（`amountOutOfRangeMessage`），
/// **不夹到边界**：玩家填 9999 却被悄悄改成 3456，比直接说清楚更让人困惑。
ManageEdit withAmount(ShopSpec const& current, std::string_view input, ShopSignConfig const& config);

/// 停业 / 恢复营业：翻转 `closed` 位，价格、件数与管理员店判定一个字不动。
///
/// 这一项**不需要输入框**（点一下就该生效），但它仍然走 `applyManageEdit`、仍然由
/// `submitManageEdit` 写牌：停业不给管理表单开第二条链，否则改价那条「重新取牌 → 重新解析
/// → 规范化 → 写牌 → 发包」迟早有一边漏一步。
/// `input` 不使用（没有输入框可读）——参数保留只为对上 `ManageChange::apply` 的类型。
ManageEdit withToggledClosed(ShopSpec const& current, std::string_view input, ShopSignConfig const& config);

} // namespace ss::shop
