#include "form/FormSupport.h"

#include "util/i18n/Locale.h"
#include "util/i18n/Message.h"

#include "Shop.h"
#include "core/economy/Service.h"

#include "ll/api/thread/ServerThreadExecutor.h"

#include "mc/world/actor/player/Player.h"

#include <utility>

namespace ss::shop {

/// 表单回调不能假设线程（DESIGN §14）：回调里**只读 XUID 与维度两个纯标量**（不读方块、不读容器、不发包），
/// 真正要读世界或写牌时先切回服务端线程。
///
/// 与交易的 `onServerThread` 同一条口径：玩家下线、换了维度、配置不可用都直接放弃——
/// 这时那块牌已经不在他眼前，弹表单或写牌都没有意义。换维度时会给玩家一句提示（发包在服务端线程）。
void onSignServerThread(Player& player, std::function<void(Player&, ShopConfig const&)> task) {
    std::string const xuid      = player.getXuid();
    int const         dimension = player.getDimensionId();

    ll::thread::ServerThreadExecutor::getDefault().execute([xuid, dimension, task = std::move(task)] {
        auto& logger       = Shop::getInstance().getSelf().getLogger();
        auto const& config = Shop::getInstance().config();
        if (!config) {
            logger.debug(ui::msg("shop.log.form.dropped_no_config", xuid));
            return;
        }
        Player* resolved = onlinePlayer(xuid);
        if (resolved == nullptr) {
            logger.debug(ui::msg("shop.log.form.dropped_offline", xuid));
            return;
        }
        if (resolved->getDimensionId() != dimension) {
            // 与管理/交易两条链同一口径：写了也不生效（那块牌子不在他眼前），
            // 但必须说一句——静默 `return` 会让玩家以为按钮坏了。
            ui::send(*resolved, ui::Tone::Warning, "shop.trade.wrong_dimension");
            return;
        }
        task(*resolved, *config);
    });
}

/// 输入框的回调结果读成文本。
///
/// 返回空 = **拿不到这个控件的值**（没这个字段，或者类型不是字符串），与「玩家留空」不是一回事：
/// 空输入框是合法的「不做这一侧生意」，读不到却是表单出问题了，两者不能混成一个空串
/// （DESIGN §10.1 的坑：把下拉当 `uint64` 读，恒为 -1）。
std::optional<std::string> formInput(ll::form::CustomFormResult const& result, std::string_view field) {
    if (!result) {
        return std::nullopt;
    }
    auto it = result->find(std::string{field});
    if (it == result->end()) {
        return std::nullopt;
    }
    if (auto const* text = std::get_if<std::string>(&it->second)) {
        return *text;
    }
    return std::nullopt;
}

/// 供应 `inputText` 的份数表单用：读不到就是空串（交给 `resolveRequestedUnits` 报「没填」）。

std::string inputText(ll::form::CustomFormResult const& result) {
    return formInput(result, kCustomUnitsField).value_or(std::string{});
}

/// 重新看一眼这块牌，得出「能不能管理」的结论。`message` 非空 = 给玩家一句话就此结束。
///
/// 每一次进管理表单、每一次提交都重新走这一遍：表单可能挂了一阵子，店可能已经换主、
/// 箱子可能已经被拆。判据不重写：身份问 `canManageShop`（spec 的「判据注意」）。

} // namespace ss::shop
