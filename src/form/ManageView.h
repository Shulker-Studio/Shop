#pragma once

#include "core/sign/SignGeometry.h"
#include "form/FormModel.h"

class Player;

namespace ss::shop {

/// 信息表单上「管理商店」按钮的入口（P1）。
///
/// 与交易相同的机制：回调里不碰世界，切回服务端线程后再重新读牌面与容器，
/// 重新判一次管理身份，然后弹管理表单。
void requestManageForm(Player& viewer, Cell const& sign);

/// 管理表单上「改一项」的入口：一张只有一个输入框的 `CustomForm`。
///
/// `action` 只能是 `ManageBuyPrice` / `ManageSellPrice` / `ManageAmount`（其他动作没有输入框）。
/// 提交时重新解析牌面、重新鉴权；成功则写正面并把这一格发给整维度（DESIGN §9.2）。
/// 三个入口共用同一条提交链，差别只在表单文案与合成函数。
void requestManageInputForm(Player& viewer, Cell const& sign, ShopFormAction action);

/// 管理表单上「停业 / 恢复营业」的入口（票 03）。
///
/// 这一项**没有输入框**：点一下就该生效，所以它不进 `CustomForm`，直接走
/// `submitManageEdit` 那条链（重新取牌 → 重新解析 → 规范化 → 写牌 → 发包）。
void requestManageToggleClosed(Player& viewer, Cell const& sign);

/// 管理表单上「更新商品名」的入口：按箱子里第一个非空槽位重写牌面第 4 行。
///
/// 牌面那行字只是商品名的**快照**，权威是箱子里的商品（DESIGN §5.3）：店主（或管理员店的 OP）
/// 换掉箱子里的东西之后，那行会一直是旧的，路人读到的是已经不卖的东西。
/// 箱子里没有商品时不写牌，只提示一句。
void requestManageRefreshItemName(Player& viewer, Cell const& sign);

} // namespace ss::shop
