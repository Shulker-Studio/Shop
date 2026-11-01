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
    case ShopBlockTouch::BreakSignSupport:
        // 拆依附方块时说“这块方块上挂着……商店牌”；主语是牌子，宾语是那块方块本身。
        return {"商店牌", "拆", "这块方块"};
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

/// 拆「商店牌依附的那一格」（DESIGN §6.2 H3e）：玩家以为自己在拆一块普通方块，
/// 不知道牌子会跟着掉、商店会没，所以这一句必须把后果说出来。
/// 主语与动词仍然从 `wordsOf` 取，保持「加一个取值只改那张表」的口径。
std::string supportBlockMessage(ShopActor actor, std::string const& ownerName, bool adminShop) {
    auto const words = wordsOf(ShopBlockTouch::BreakSignSupport);

    std::string message = "这块方块上挂着";
    if (adminShop) {
        message += "管理员商店的";
    } else if (actor == ShopActor::NoRecordedOwner) {
        message += "没有记录店主的";
    } else if (!ownerName.empty()) {
        message += " " + ownerName + " 的";
    } else {
        message += "别人的";
    }
    message += std::string(words.subject);
    message += "，拆了牌子会掉下来、商店也就没了";
    // 管理员商店与无主牌都只给 OP（§9.2 / §11），写“店主”会让人去找一个不存在的人。
    // 括号不能省：`+=` 的优先级高于 `?:`，省了它就会把条件本身当成字符拼进去。
    bool const operatorOnly = adminShop || actor == ShopActor::NoRecordedOwner;
    message += (operatorOnly ? "；只有 OP 能" : "；只有店主（或 OP）能");
    message += std::string(words.verb);
    message += std::string(words.object);
    return message;
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
    case ShopBlockTouch::BreakSignSupport:
        if (canManageShop(context.actor)) {
            return {};
        }
        break;
    }

    if (context.touch == ShopBlockTouch::BreakSignSupport) {
        return {false, supportBlockMessage(context.actor, context.ownerName, context.adminShop)};
    }
    if (context.actor == ShopActor::NoRecordedOwner) {
        return {false, noRecordedOwnerMessage(context.touch)};
    }
    return {false, ownerOnlyMessage(context.touch, context.ownerName, context.adminShop)};
}

} // namespace ss::shop
