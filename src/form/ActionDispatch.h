#pragma once

#include "core/sign/SignGeometry.h"
#include "form/FormModel.h"

class Player;

namespace ss::shop {

/// 按钮按下之后干什么：买卖各自弹份数表单，管理入口进管理表单，管理表单上的按钮进各自的输入框。
///
/// 这里是**动作分发**的唯一一处：加动作时编译器会提醒这里漏了一个。
/// 回调可能在非主线程触发（DESIGN §14），所以这里不碰世界，只把工作交给 `request*`。
void onButton(Player& player, Cell const& sign, ShopFormAction action);

} // namespace ss::shop
