#include "util/i18n/Message.h"

#include "util/i18n/Locale.h"
#include "util/i18n/Text.h"

#include "mc/network/packet/TextPacket.h"
#include "mc/world/actor/player/Player.h"

namespace ss::shop::ui {
namespace {

std::string_view toneColor(Tone tone) {
    switch (tone) {
    case Tone::Success:
        return kSuccess;
    case Tone::Failure:
        return kFailure;
    case Tone::Warning:
        return kWarning;
    case Tone::Hint:
        return kHint;
    }
    return kHint;
}

} // namespace

std::string line(Tone tone, std::string_view body) {
    std::string out{prefix()};
    out += toneColor(tone);
    // 命令输出没有「玩家语言」可言，用默认 locale；已经是中文的串查不到，原样返回。
    out += translate(body, {});
    return out;
}

/// 发一条已经带好 `§` 码的文本。
///
/// **必须走 `TextPacketType::Raw`**：`Player::sendMessage` 发的是 Chat 类型，
/// 客户端会把聊天消息当纯文本渲染（玩家能伪造格式就乱了），`§` 码连同后面的字符
/// 一起被吃掉——实机上表现为「一句话颜色全没、还少了几个字」。
std::string line(Tone tone, ss::shop::Text const& text) {
    return line(tone, renderText(text, {}));
}

void sendRaw(Player& player, std::string message) {
    auto packet = TextPacket::createRawMessage(message);
    player.sendNetworkPacket(packet);
}

void send(Player& player, Tone tone, ss::shop::Text const& text) {
    sendRaw(player, line(tone, renderText(text, player.getLocaleCode())));
}

void send(Player& player, Tone tone, std::string_view chinese) {
    sendRaw(player, line(tone, translate(chinese, player.getLocaleCode())));
}

} // namespace ss::shop::ui
