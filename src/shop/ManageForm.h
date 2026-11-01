#pragma once

#include "shop/ShopInfo.h"
#include "shop/ShopSign.h"

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
std::string shopManageFormContent(ShopInfoData const& data, bool waxed);

/// 管理表单的按钮：改买入价 / 改卖出价 / 改每份件数（03 再加停业 / 恢复营业）。
///
/// 文案带当前值：点进去之前就该知道现在是几。
std::vector<ShopFormButton> shopManageFormButtons(ShopInfoData const& data);

/// 一次管理表单改动的结果。
struct ManageEdit {
    std::optional<ShopSpec> spec;  ///< 改完的商店定义；空 = 这次不能改
    std::string             error; ///< `spec` 为空时给玩家的中文原因

    [[nodiscard]] bool ok() const { return spec.has_value(); }
};

/// 改买入价：当前的商店定义 + 输入框原文 → 新的商店定义或原因。
///
/// 输入只收**非负整数或空串**（空 = 这一侧不做生意）：方向由「哪个输入框」表达，
/// 所以牌面写法 `B 10`、`free`、`1.5K` 一律不收。整数必须整串解析完，溢出算非法——
/// 不截断、不静默当 0。
///
/// 三条校验只在一处，就在这两个价格函数的内部实现里：输入非法、
/// 两侧都清空（第 3 行会变空，`looksLikeShopSign` 为假 → 牌与箱子整体失去保护，spec D6）、
/// 卖出价高于买入价（问 `sellPriceHigherThanBuy`，与手写编辑同一条规则）。
///
/// 输出只给 `ShopSpec`：牌面文本由调用方用 `formatShopSign` 拼（规范化只有那一处），
/// 第 1 行（店主名）与第 4 行（商品名）也不归这里管。其余字段原样带过去。
ManageEdit withBuyPrice(ShopSpec const& current, std::string_view input, ShopSignConfig const& config);

/// 改卖出价：与 `withBuyPrice` 同一套解析与校验，只是换一侧（留空 = 这一侧不收购）。
ManageEdit withSellPrice(ShopSpec const& current, std::string_view input, ShopSignConfig const& config);

/// 改每份件数：当前的商店定义 + 输入框原文 → 新的商店定义或原因。
///
/// 件数的文法不在这里写第二遍：直接问 `parseAmountLine`（牌面第 2 行的权威解析器），
/// 所以「什么叫合法件数」与手写编辑只有一份。件数没有「留空 = 这一侧不做生意」这一档——
/// 空、非整数、小于 1、大于 `config.maxShopAmount` 一律按同一条范围提示拒绝（`amountOutOfRangeMessage`），
/// **不夹到边界**：玩家填 9999 却被悄悄改成 3456，比直接说清楚更让人困惑。
ManageEdit withAmount(ShopSpec const& current, std::string_view input, ShopSignConfig const& config);

} // namespace ss::shop
