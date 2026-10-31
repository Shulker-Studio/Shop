#pragma once

#include "shop/ShopInfo.h"
#include "shop/TradePlan.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ss::shop {

/// 「买几份」表单的模型（DESIGN §10.1 第 3 步）。
///
/// 纯逻辑：只是下拉框的档位与上限，读不到世界，也不知道表单长什么样。
/// 提交时**不信任**这里的值：`planBuy` 会按最新可成交份数/余额/背包再算一遍（DESIGN §10.4）。
struct BuyFormModel {
    /// 下拉框的一档。`units == 0` 表示「自定义份数」，读输入框。
    struct Choice {
        int         units = 0;
        std::string label;
    };

    std::vector<Choice> choices;
    /// 这次最多能成交几份：maxShopAmount、可成交份数、余额、背包取最小（DESIGN §10.1 第 3 步）。
    int maxUnits = 1;
};

/// 建下拉框：上限以内的 `1 / 2 / 4`，最后一项永远是「自定义份数」。
///
/// `maxUnits < 1` 时只留「自定义份数」——这种店本来就买不了，调用方应该先拒绝，
/// 这里不把 0 夹成 1 硬造一个必然失败的档位。
BuyFormModel makeBuyFormModel(int maxUnits);

/// 下拉框的选项文案，顺序与 `model.choices` 一致。
std::vector<std::string> buyFormOptions(BuyFormModel const& model);

/// 表单正文：每份件数、买入价、余额、箱子里的货，再逐档列出**总件数与总价**。
///
/// 每一档的金额都写出来，选哪一档看到的数就是最终扣的数，页面不需要实时刷新
/// 也能保证「显示的总价 = 实际扣款」，且恒为整数（DESIGN §10.1）。
std::string buyFormContent(ShopInfoData const& data, BuyFormModel const& model);

/// 成交条件为什么会变（DESIGN §10.3）。三样里任何一样变了都要玩家重新点头。
enum class ChangeReason : std::uint8_t {
    Units, ///< 能成交的份数变少了
    Items, ///< 「一份的件数」变了（份数与总价可能都没变）
    Price, ///< 单价变了
};

/// 成交条件变了的确认正文：原因 + 真实份数、件数、总价（DESIGN §10.3）。
///
/// 份数表单上看到的是「弹出那一刻」的数；两屏之间箱子、店主改价或改份量都会让它过期。
/// 这时先把重算后的真实值摆出来让玩家确认，再执行——不能拿旧数字直接扣款。
std::string buyConfirmContent(ShopInfoData const& data, BuyPlan const& plan, ChangeReason reason);

/// 提交时的解析结果：`units` 为空时 `error` 是给玩家看的中文原因。
struct RequestedUnits {
    std::optional<int> units;
    std::string        error;
};

/// 把「下拉框选中项 + 自定义输入」解析成请求份数。
///
/// `choiceIndex` 越界、自定义输入为空/非整数/超出 `maxUnits` 都算非法，
/// 不允许悄悄夹到上限——玩家填 100 却买到 4 份比直接报错更让人困惑。
RequestedUnits resolveRequestedUnits(BuyFormModel const& model, int choiceIndex, std::string_view customText);

} // namespace ss::shop
