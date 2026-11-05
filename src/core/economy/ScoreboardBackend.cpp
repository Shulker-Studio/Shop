#include "core/economy/ScoreboardBackend.h"

#include "core/economy/Service.h"

#include "ll/api/service/Bedrock.h"
#include "ll/api/service/PlayerInfo.h"

#include "mc/deps/nbt/CompoundTag.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/storage/DBStorage.h"
#include "mc/world/level/storage/db_helpers/Category.h"
#include "mc/world/scores/Objective.h"
#include "mc/world/scores/ObjectiveCriteria.h"
#include "mc/world/scores/PlayerScoreSetFunction.h"
#include "mc/world/scores/ScoreInfo.h"
#include "mc/world/scores/Scoreboard.h"
#include "mc/world/scores/PlayerScoreboardId.h"
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
    if (player != nullptr) {
        // 从来没有在这个目标上留过分值的玩家**可能连记分板身份都没有**（真机 2026-11-01：
        // 这种玩家一交易就报读不到余额）。原版 `/scoreboard players set <玩家> <目标> 0` 自己
        // 就是现场建身份，所以这里照做：没有身份就用 `createScoreboardId` 建一个，
        // 于是新玩家的余额是 0 而不是「读不到」。这也是其它记分板经济实现的通行做法。
        ScoreboardId const* id = &scoreboard.getScoreboardId(*player);
        if (*id == ScoreboardId::INVALID()) {
            id = &scoreboard.createScoreboardId(*player);
        }
        if (*id == ScoreboardId::INVALID()) {
            return std::nullopt;
        }
        return ScoreTarget{&scoreboard, objective, id};
    }

    // 离线账户（店主不在线时买卖要照常记账）。记分板身份的内部键是
    // `PlayerScoreboardId`（= 玩家的 `ActorUniqueID`），而这个 ID **存在玩家自己的存档里**，
    // 所以人不在线也能拿到：照着 LSE `mc.addPlayerScore(uuid, …)` 的做法
    // （LegacyScriptEngine/src/legacy/api/PlayerAPI.cpp）走一遍——
    //
    //   XUID → 玩家表拿 uuid → DBStorage 的 `player_<uuid>` → `ServerId` → 存档里的 `UniqueID`
    //        → `scoreboard.getId(PlayerScoreboardId(uniqueId))` → 写分
    //
    // 用 ID 而不是名字：改名、重名都不影响，也不会把分数记到同名的假玩家身份上。
    // 任何一步拿不到就返回空（= 读不到余额，交易被拒），绝不猜。
    auto const entry = ll::service::PlayerInfo::getInstance().fromXuid(account);
    if (!entry) {
        return std::nullopt;
    }
    auto storage = ll::service::getDBStorage();
    if (!storage) {
        return std::nullopt;
    }
    auto const playerKey = "player_" + entry->uuid.asString();
    if (!storage->hasKey(playerKey, DBHelpers::Category::Player)) {
        return std::nullopt;
    }
    auto playerTag = storage->getCompoundTag(playerKey, DBHelpers::Category::Player);
    if (!playerTag || !playerTag->contains("ServerId", Tag::String)) {
        return std::nullopt;
    }
    std::string const serverId = (*playerTag)["ServerId"].get<StringTag>();
    if (serverId.empty() || !storage->hasKey(serverId, DBHelpers::Category::Player)) {
        return std::nullopt;
    }
    auto serverTag = storage->getCompoundTag(serverId, DBHelpers::Category::Player);
    if (!serverTag || !serverTag->contains("UniqueID")) {
        return std::nullopt;
    }
    ScoreboardId const& id = scoreboard.getId(PlayerScoreboardId(static_cast<int64>((*serverTag)["UniqueID"])));
    if (id == ScoreboardId::INVALID()) {
        return std::nullopt;
    }
    return ScoreTarget{&scoreboard, objective, &id};
}

} // namespace

ScoreboardBackend::ScoreboardBackend(std::string objectiveName) : mObjectiveName(std::move(objectiveName)) {}

std::shared_ptr<ScoreboardBackend>
ScoreboardBackend::create(std::string objectiveName, bool createIfMissing, economy::ProbeIssue& issue) {
    issue = economy::ProbeIssue::ObjectiveMissing;
    auto level = ll::service::bedrock::getLevel();
    if (!level) {
        // 世界还没起来：调用方应该先确认过，这里只是不拿空 `Level` 去崩。
        return nullptr;
    }

    auto& scoreboard = level->getScoreboard();
    if (scoreboard.getObjective(objectiveName) == nullptr) {
        if (!createIfMissing) {
            issue = economy::ProbeIssue::ObjectiveMissing;
            return nullptr;
        }
        // 判据用原版现成的 `dummy`（`/scoreboard objectives add <名字> dummy` 同一个判据）：
        // 自己造一个 `ObjectiveCriteria` 只能靠猜它的构造方式，而引擎自己的判据清单是权威。
        std::string const criteriaName = "dummy";
        ObjectiveCriteria* criteria     = scoreboard.getCriteria(criteriaName);
        if (criteria == nullptr) {
            issue = economy::ProbeIssue::ObjectiveCreateFailed;
            return nullptr;
        }
        // 显示名就用目标名：这个目标不是给人看的榜单，是账本，名字对得上才查得清。
        if (scoreboard.addObjective(objectiveName, objectiveName, *criteria) == nullptr) {
            issue = economy::ProbeIssue::ObjectiveCreateFailed;
            return nullptr;
        }
    }
    return std::shared_ptr<ScoreboardBackend>{new ScoreboardBackend(std::move(objectiveName))};
}

std::optional<std::int64_t> ScoreboardBackend::get(economy::Account const& account) const {
    auto const target = resolveTarget(mObjectiveName, account);
    if (!target) {
        // 账户不可记账（不在线、目标没了、没有记分板身份）：返回空，不能报成 0 元。
        return std::nullopt;
    }
    ScoreInfo const score = target->objective->getPlayerScore(*target->id);
    // 目标在、人也在，只是还没有分值：这就是 0 元，不是读不到。
    return score.mValid ? score.mValue : 0;
}

bool ScoreboardBackend::writeScore(economy::Account const& account, int value) const {
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

bool ScoreboardBackend::set(economy::Account const& account, std::int64_t amount) {
    auto const value = economy::toScoreValue(amount);
    if (!value) {
        return false; // 超出 int32：拒绝，不截断（DESIGN §8.2）
    }
    return writeScore(account, *value);
}

bool ScoreboardBackend::add(economy::Account const& account, std::int64_t amount) {
    if (amount <= 0) {
        return false;
    }
    auto const current = get(account);
    if (!current) {
        return false; // 读不到当前分值就没法写目标值（`set` 要的是结果，不是增量）
    }
    // 先在 `int64` 里把溢出堵住，再交给 `set` 判 `int32` 范围：两关都过才写。
    if (*current > std::numeric_limits<std::int64_t>::max() - amount) {
        return false;
    }
    return set(account, *current + amount);
}

bool ScoreboardBackend::reduce(economy::Account const& account, std::int64_t amount, bool allowNegative) {
    if (amount <= 0) {
        return false;
    }
    auto const current = get(account);
    if (!current) {
        return false;
    }
    if (!allowNegative && *current < amount) {
        return false; // 余额不足：一个字节都不写（DESIGN §8.2）
    }
    return set(account, *current - amount);
}

} // namespace ss::shop
