#include "mod/ScoreboardEconomy.h"

#include "mod/EconomyService.h"

#include "ll/api/service/Bedrock.h"

#include "mc/world/actor/player/Player.h"
#include "mc/world/level/Level.h"
#include "mc/world/scores/Objective.h"
#include "mc/world/scores/PlayerScoreSetFunction.h"
#include "mc/world/scores/ScoreInfo.h"
#include "mc/world/scores/Scoreboard.h"
#include "mc/world/scores/ScoreboardId.h"
#include "mc/world/scores/ScoreboardOperationResult.h"

#include <limits>
#include <optional>

namespace ss::shop {
namespace {

/// 一次「目标 + 账户分值」的解析结果。
struct ScoreTarget {
    Scoreboard*         scoreboard = nullptr;
    Objective*          objective  = nullptr;
    ScoreboardId const* id         = nullptr;
};

/// 目标与账户都必须能解析出来：目标不存在、玩家不在线、没有记分板身份都算不可记账。
///
/// 记分板的关键限制就在这里：`Scoreboard::getScoreboardId` 只接 `Actor`，
/// 离线玩家根本没有 ID 可写，所以这个后端天生不支持离线（DESIGN §8.5）。
std::optional<ScoreTarget> resolveTarget(std::string const& objectiveName, economy::Account const& account) {
    auto level = ll::service::bedrock::getLevel();
    if (!level) {
        return std::nullopt;
    }
    auto&      scoreboard = level->getScoreboard();
    Objective* objective  = scoreboard.getObjective(objectiveName);
    if (objective == nullptr) {
        return std::nullopt;
    }
    Player* player = onlinePlayer(account);
    if (player == nullptr) {
        return std::nullopt;
    }
    ScoreboardId const& id = scoreboard.getScoreboardId(*player);
    if (id == ScoreboardId::INVALID()) {
        return std::nullopt;
    }
    return ScoreTarget{&scoreboard, objective, &id};
}

} // namespace

ScoreboardEconomy::ScoreboardEconomy(std::string objectiveName) : mObjectiveName(std::move(objectiveName)) {}

std::shared_ptr<ScoreboardEconomy> ScoreboardEconomy::create(std::string objectiveName) {
    auto level = ll::service::bedrock::getLevel();
    if (!level) {
        return nullptr;
    }
    if (level->getScoreboard().getObjective(objectiveName) == nullptr) {
        return nullptr; // 目标不存在：调用方降级（自动建目标是 10 的事）
    }
    return std::shared_ptr<ScoreboardEconomy>{new ScoreboardEconomy(std::move(objectiveName))};
}

std::int64_t ScoreboardEconomy::get(economy::Account const& account) const {
    auto const target = resolveTarget(mObjectiveName, account);
    if (!target) {
        return 0;
    }
    ScoreInfo const score = target->objective->getPlayerScore(*target->id);
    return score.mValid ? score.mValue : 0;
}

bool ScoreboardEconomy::writeScore(economy::Account const& account, int value) const {
    auto const target = resolveTarget(mObjectiveName, account);
    if (!target) {
        return false;
    }
    ScoreboardOperationResult result = ScoreboardOperationResult::Success;
    // 一律用 `Set` 写目标值：加、减都先在 `int64` 里算好，引擎不会替我们溢出
    // （`PlayerScoreSetFunction::Add` 会让引擎自己加，溢出时未必报得出来）。
    target->scoreboard->modifyPlayerScore(result, *target->id, *target->objective, value, PlayerScoreSetFunction::Set);
    return result == ScoreboardOperationResult::Success;
}

bool ScoreboardEconomy::set(economy::Account const& account, std::int64_t amount) {
    auto const value = economy::toScoreValue(amount);
    if (!value) {
        return false; // 超出 int32：拒绝，不截断（DESIGN §8.2）
    }
    return writeScore(account, *value);
}

bool ScoreboardEconomy::add(economy::Account const& account, std::int64_t amount) {
    if (amount <= 0) {
        return false;
    }
    auto const current = get(account);
    // 先在 `int64` 里把溢出堵住，再交给 `set` 判 `int32` 范围：两关都过才写。
    if (current > std::numeric_limits<std::int64_t>::max() - amount) {
        return false;
    }
    return set(account, current + amount);
}

bool ScoreboardEconomy::reduce(economy::Account const& account, std::int64_t amount, bool allowNegative) {
    if (amount <= 0) {
        return false;
    }
    auto const current = get(account);
    if (!allowNegative && current < amount) {
        return false; // 余额不足：一个字节都不写（DESIGN §8.2）
    }
    return set(account, current - amount);
}

} // namespace ss::shop
