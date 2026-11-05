#pragma once

#include "core/sign/SignProtection.h"

#include <cstdint>

namespace ss::shop {

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

} // namespace ss::shop
