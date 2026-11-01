#include "shop/ContainerProtection.h"

#include <string>
#include <string_view>

namespace ss::shop {
namespace {

/// 一种操作的说法。三种操作的全部文案差异都从这张表来：加一个取值时只改这里，
/// 不会出现「subject 改了、verb 忘了」的半句话。
struct TouchWords {
    std::string_view subject; ///< 「商店箱」/「商店牌」
    std::string_view verb;    ///< 「打开」/「拆掉」
    /// 「无主牌」提示里第二次提到这件事时的短称——那句话开头已经说过「这块商店牌」。
    std::string_view object;
};

constexpr TouchWords wordsOf(ShopBlockTouch touch) {
    switch (touch) {
    case ShopBlockTouch::OpenChest:
        return {"商店箱", "打开", "这个箱子"};
    case ShopBlockTouch::BreakChest:
        return {"商店箱", "拆掉", "这个箱子"};
    case ShopBlockTouch::BreakSign:
        return {"商店牌", "拆掉", "这块牌"};
    }
    return {"商店箱", "打开", "这个箱子"};
}

/// 非店主被拦下的提示。管理员商店单独一句：它的管理权只给 OP（DESIGN §11），
/// 写「只有店主能…」会让人去找一个不存在的人。
std::string ownerOnlyMessage(ShopBlockTouch touch, std::string const& ownerName, bool adminShop) {
    auto const words = wordsOf(touch);

    if (adminShop) {
        return "这是管理员商店的" + std::string(words.subject) + "，只有 OP 能" + std::string(words.verb);
    }
    if (ownerName.empty()) {
        // 牌上没记名字但记了 XUID（或 PlayerInfo 查不到）：不拼「是  的商店箱」这种半句话。
        return "这是别人的" + std::string(words.subject) + "，只有店主（或 OP）能" + std::string(words.verb);
    }
    return "这是 " + ownerName + " 的" + std::string(words.subject) + "，只有店主（或 OP）能" + std::string(words.verb);
}

/// 没记下店主 XUID 的牌（DESIGN §6.5 第 3 条）：谁都不是店主，只给 OP。
std::string noRecordedOwnerMessage(ShopBlockTouch touch) {
    auto const words = wordsOf(touch);
    return "这块商店牌没有记录店主，只有 OP 能" + std::string(words.verb) + std::string(words.object);
}

/// 店主被 `allowOwnerOpenChest = false` 挡下：提示要点出服主的开关，
/// 否则玩家会以为是自己没有权限。动词与宾语仍然从 `wordsOf` 来，不另写一份。
std::string ownerOpenDisabledMessage() {
    auto const words = wordsOf(ShopBlockTouch::OpenChest);
    return "服主关闭了店主开箱（allowOwnerOpenChest），只有 OP 能" + std::string(words.verb)
         + std::string(words.object);
}

} // namespace

ShopBlockDecision planShopBlockTouch(ShopBlockTouchContext const& context) {
    // 「管理身份」只有一处口径：店主与 OP（`canManageShop`，§9.2 / §11）。
    // 开箱多一道配置闸，所以它不走这个判据的短路。
    switch (context.touch) {
    case ShopBlockTouch::OpenChest:
        if (context.actor == ShopActor::Operator) {
            return {};
        }
        if (context.actor == ShopActor::Owner) {
            return context.allowOwnerOpenChest ? ShopBlockDecision{}
                                               : ShopBlockDecision{false, ownerOpenDisabledMessage()};
        }
        break;
    case ShopBlockTouch::BreakChest:
    case ShopBlockTouch::BreakSign:
        if (canManageShop(context.actor)) {
            return {};
        }
        break;
    }

    if (context.actor == ShopActor::NoRecordedOwner) {
        return {false, noRecordedOwnerMessage(context.touch)};
    }
    return {false, ownerOnlyMessage(context.touch, context.ownerName, context.adminShop)};
}

} // namespace ss::shop
