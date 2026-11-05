#include "core/shop/ShopFormRole.h"

namespace ss::shop {

ShopFormRole shopFormRoleOf(ShopActor actor, bool adminShop) {
    // 管理员店的管理权只给 OP（§11），但那不等于「OP 不能买东西」：它的对手方是系统账户（§7.4），
    // 店主不在这里。
    if (adminShop) {
        return ShopFormRole::Visitor;
    }
    return canManageShop(actor) ? ShopFormRole::Manager : ShopFormRole::Visitor;
}

} // namespace ss::shop
