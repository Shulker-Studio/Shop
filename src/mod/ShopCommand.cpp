#include "mod/ShopCommand.h"

#include "mod/DebugLog.h"
#include "mod/EconomyService.h"
#include "mod/HookSlots.h"
#include "mod/LogFile.h"
#include "mod/Shop.h"

#include "ll/api/command/CommandHandle.h"
#include "ll/api/command/CommandRegistrar.h"
#include "ll/api/mod/Mod.h"
#include "ll/api/mod/ModManagerRegistry.h"

#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/server/commands/CommandPermissionLevel.h"

#include <optional>
#include <span>
#include <string>

#include "ll/api/data/Version.h"

namespace ss::shop {

/// `/shop debug <value>` 的参数包。`on` / `off` 只是两个字符串，不引入枚举参数：
/// 命令层越薄，改起来越不怕碰引擎的枚举转换规则。
///
/// 必须放在**具名**命名空间里：`overload<T>()` 会用 `boost::pfr` 反射 `T` 的成员，
/// 内部要取一个同名的 `extern` 假对象，内部链接的类型过不了这一关（clang 会报
/// "used but not defined ... does not have linkage"）。
struct DebugParam {
    std::string value;
};

namespace {

/// `Manifest::version` 是可选的：发布包一定会写，开发期手拼的 manifest 可能没有，
/// 所以读不到就如实写「未知」。
std::string versionText(std::optional<ll::data::Version> const& version) {
    return version.has_value() ? version->to_string() : std::string{"未知"};
}

std::string const& selfVersion() {
    static std::string const version = versionText(Shop::getInstance().getSelf().getManifest().version);
    return version;
}

/// iListenAttentively 的版本。拿不到就如实写「未知」——它是硬依赖，但"版本号读不到"
/// 与"没装"是两件事，不该在这里编一个数字。
std::string dependencyVersion(std::string_view name) {
    auto& registry = ll::mod::ModManagerRegistry::getInstance();
    if (!registry.hasMod(name)) {
        return "未安装";
    }
    if (auto mod = registry.getMod(name)) {
        return versionText(mod->getManifest().version);
    }
    return "未知";
}

/// 浮点配置的可读写法：`0.0` 显示成 `0`，`0.1` 还是 `0.1`。
std::string decimalText(double value) {
    std::string text = std::to_string(value);
    if (text.find('.') != std::string::npos) {
        text.erase(text.find_last_not_of('0') + 1);
        if (!text.empty() && text.back() == '.') {
            text.pop_back();
        }
    }
    return text;
}

std::string hookLines(std::string_view title, std::span<HookSlot const> slots) {
    std::string out{"hooks（"};
    out.append(title);
    out += "）：\n";
    for (auto const& slot : slots) {
        out += slot.attached ? "  [x] " : "  [ ] ";
        out += slot.name;
        out += slot.attached ? "  已尝试注册，attach 返回 " + std::to_string(slot.lastReturn) : "  未尝试注册";
        out += '\n';
    }
    return out;
}

void reportStatus(CommandOutput& output) {
    auto const& config = Shop::getInstance().config();
    if (!config) {
        output.error("Shop 的配置不可用，模组没有启用");
        return;
    }

    std::string out{"Shop "};
    out += selfVersion();
    out += "（iListenAttentively ";
    out += dependencyVersion("iListenAttentively");
    out += "）\n";

    out += "配置：maxShopAmount=" + std::to_string(config->maxShopAmount);
    out += " allowPartialTransactions=" + std::string{config->allowPartialTransactions ? "true" : "false"};
    out += " ignoreCreativeMode=" + std::string{config->ignoreCreativeMode ? "true" : "false"};
    out += " allowOwnerOpenChest=" + std::string{config->allowOwnerOpenChest ? "true" : "false"};
    out += " requireOwnerOnline=" + std::string{config->economy.requireOwnerOnline ? "true" : "false"};
    out += " shopTaxRate=" + decimalText(config->tax.shopTaxRate);
    out += " shopInteractionCooldownMs=" + std::to_string(config->shopInteractionCooldownMs);
    out += " economy.backend=" + config->economy.backend;
    out += '\n';

    // 交易流水（`p1-transaction-log` D4）：两个配置项 + 现在到底开没开。
    // 「开」不等于「能写」：目录建不出来、磁盘满的时候每一笔都会 warn（同进程一次），
    // 所以这里只报**配置与目录**，不编一个健康状态。
    out += "交易流水：";
    out += config->transactionLog.enabled ? "开" : "关（业务流水与 discarded 一起停）";
    out += " compressAfterDays=" + std::to_string(config->transactionLog.compressAfterDays);
    out += config->transactionLog.compressAfterDays == 0 ? "（永不压缩）" : "（超期压成 .jsonl.gz，永不删除）";
    out += "，目录 " + transactionLogDirectory().string() + " 与 " + discardLogDirectory().string();
    out += '\n';

    // **只读**：不在这里触发解析。解析由 `ServerStartedEvent` 或第一次动钱负责，
    // 敲一条查询命令不该顺手在记分板上建目标。
    auto const& selection  = economySelectionText();
    out                   += "经济后端：";
    out                   += selection.empty() ? "尚未解析（世界未就绪）" : selection;
    out                   += '\n';

    out += hookLines("破坏防护", destructionHookSlots());
    out += hookLines("内容防盗", contentHookSlots());

    out += "调试日志：";
    out += shopDebugEnabled() ? "开（/shop debug off 关闭）" : "关（/shop debug on 打开）";

    output.success(out);
}

void applyDebugSwitch(CommandOutput& output, std::string const& value) {
    if (value == "on") {
        setShopDebugEnabled(true);
        output.success("调试日志已打开：每次牌面判定、保护拦截、交易结果、内容防盗拦截都会打一行 [shop/dbg]");
        return;
    }
    if (value == "off") {
        setShopDebugEnabled(false);
        output.success("调试日志已关闭");
        return;
    }
    output.error("用法：/shop debug <on|off>");
}

} // namespace

void registerShopCommand() {
    // 重载模组（`/ll reload Shop`）会再跑一遍 enable()。命令句柄是全局的，
    // 再挂一次 `overload` 会让一条 `/shop status` 执行两遍，所以只挂一次。
    static bool registered = false;
    if (registered) {
        return;
    }
    registered = true;

    auto& logger = Shop::getInstance().getSelf().getLogger();
    auto& command =
        ll::command::CommandRegistrar::getInstance(/*isClientSide=*/false)
            .getOrCreateCommand("shop", "商店模组的状态查询与调试开关", CommandPermissionLevel::GameDirectors);

    command.overload().text("status").execute([](CommandOrigin const&, CommandOutput& output) {
        reportStatus(output);
    });

    command.overload<DebugParam>().text("debug").required("value").execute(
        [](CommandOrigin const&, CommandOutput& output, DebugParam const& param) {
            applyDebugSwitch(output, param.value);
        }
    );

    // 注册本身没有返回值可判；这里只在拿不到命令句柄时留一条痕迹。
    logger.debug("已注册 /shop 命令（status / debug）");
}

} // namespace ss::shop
