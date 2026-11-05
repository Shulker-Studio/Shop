#pragma once

#include "ll/api/form/CustomForm.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>

class Player;

namespace ss::shop {

struct ShopConfig;

/// 表单里几个控件的名字。读回来时用它们取值，写错就是静默拿不到值，所以只写一次。
/// 买卖两张份数表单共用前两个：同一时刻只会有一张开着。
inline constexpr std::string_view kUnitChoiceField  = "units";
inline constexpr std::string_view kCustomUnitsField = "custom_units";
inline constexpr std::string_view kBuyPriceField    = "buy_price";
inline constexpr std::string_view kSellPriceField   = "sell_price";
inline constexpr std::string_view kAmountField      = "amount";

/// 提交时牌子已经不在了（改价与改件数共用这一句，所以说「这次的改动」）。
inline constexpr std::string_view kSignGoneMessage = "shop.protection.sign_gone";

/// 表单回调不能假设线程（DESIGN §14）：回调里**只读 XUID 与维度两个纯标量**
/// （不读方块、不读容器、不发包），真正要读世界或写牌时先切回服务端线程。
///
/// 与交易的 `onServerThread` 同一条口径：玩家下线、换了维度、配置不可用都直接放弃——
/// 这时那块牌已经不在他眼前，弹表单或写牌都没有意义。换维度时会给玩家一句提示（发包在服务端线程）。
void onSignServerThread(Player& player, std::function<void(Player&, ShopConfig const&)> task);

/// 输入框的回调结果读成文本。
///
/// 返回空 = **拿不到这个控件的值**（没这个字段，或者类型不是字符串），与「玩家留空」不是一回事：
/// 空输入框是合法的「不做这一侧生意」，读不到却是表单出问题了，两者不能混成一个空串
/// （DESIGN §10.1 的坑：把下拉当 `uint64` 读，恒为 -1）。
std::optional<std::string> formInput(ll::form::CustomFormResult const& result, std::string_view field);

/// 输入框回调读成字符串（玩家留空 = 空串）。
std::string inputText(ll::form::CustomFormResult const& result);

} // namespace ss::shop
