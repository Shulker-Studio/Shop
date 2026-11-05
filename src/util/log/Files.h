#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace ss::shop {

/// 日志根目录：`mods/Shop/logs/`。
///
/// 用 `getModDir()`（= 模组目录 `mods/Shop`）拼，**不是** `getConfigDir()`：
/// 后者是 `mods/Shop/config`（配置文件所在的那一层），拿它拼会变成
/// `mods/Shop/config/logs/` —— 与 README、票面和服主的预期都不一样。
/// 也不自己写死 `mods/Shop`：模组装在哪由加载器决定。
std::filesystem::path logDirectory();

/// 业务流水目录：`mods/Shop/logs/transactions/`。
///
/// 两类记录分目录放，因为它们**体量差两个数量级**：业务流水一行几十字节，销毁留档一行可能几十 KB
/// （完整 SNBT）。分开之后 `du -sh logs/*` 一眼能看出是谁在涨，`zcat transactions/*.gz` 查历史账时
/// 也不会顺手把几百 MB 的 NBT 一起解出来。文件名仍是 `YYYY-MM-DD.jsonl`（按天轮转），
/// 不再叠年月子目录：一年才 365 个文件，目录不会失控。
std::filesystem::path transactionLogDirectory();

/// 销毁留档目录：`mods/Shop/logs/discarded/`。见 `transactionLogDirectory()`。
std::filesystem::path discardLogDirectory();

/// 把旧布局（所有文件平铺在 `logs/` 根下、带 `transactions-` / `discarded-` 前缀）搬进新布局。
///
/// 幂等、只在模组启用时跑一次、失败只 `warn`：搬不动（目标已存在、文件被占用）就留在原地，
/// **不覆盖**任何已有文件。只搬本模组写出来的名字（`relocatedLogFile` 判定），
/// 那个目录里服主自己放的东西不会被碰。
void migrateLegacyLogs();

/// 启动时把过期的日志文件压成 `.gz`（`transactionLog.compressAfterDays`）。
///
/// **只在启动时调一次**，不在写日志的路径上：早先每落一行流水就扫两个目录、
/// 必要时当场做一次同步 gzip，那笔 CPU 与磁盘 I/O 全落在「玩家刚成交」那一拍的 tick 线程上。
/// 压缩不需要那么准时——一天一次就够，而启动时服主本来就在等。
///
/// 只在 `transactionLog.enabled` 时做（关掉流水的服主不会期望模组去动他的目录）。
/// 失败只 `warn` 且不删原文件，下次启动重试。
void compressOldLogsOnStartup();

/// 把一批**已经是完整 NDJSON 行**的文本追加到 `dir / fileName`。
///
/// 这是**所有日志文件的唯一出路**（业务流水与销毁留档都走它），
/// 所以三件与「这条日志能不能落盘」有关的事都只在这里判一次：
///
/// 1. **总开关**：`transactionLog.enabled == false` 时直接返回，两个文件一起停；
/// 2. **落盘**：每次调用都「建目录 → 开 → 写 → flush → 关」，**不缓存句柄**——文件按天轮转
///    （跨天就是另一个名字），缓存「当前文件」就要额外维护「跨天了没有」「上次开失败了要不要重开」
///    两种状态，而这里一天最多写几次；
/// 3. **回滚**：写入失败（磁盘满、盘被拔）时截回写入前的大小，不让半行 JSON 把当天的流水弄成读不出来。
///
/// **不在这里做压缩**：压缩是启动时的一次活（`compressOldLogsOnStartup`），
/// 不该让落一行流水去付 gzip 与目录扫描的账。
///
/// 失败只 `warn`，而且按失败种类**同一进程只提示一次**：日志不是交易的一部分，磁盘满了也
/// 必须让生意成交；但每笔交易刷一条 warn 会把真正的信息淹掉（spec 的决定）。
///
/// @return 这一段是否真的写进去了。调用方**没有**补偿动作（写失败不回滚交易），
///         返回值只用于「写成功之后再顺手做下一件事」这一种判断。
bool appendLogLines(
    std::filesystem::path const&    dir,
    std::string const&              fileName,
    std::vector<std::string> const& lines
);

/// 清掉「这一种失败已经提示过」的进程级记录。
///
/// 模组重载（`/ll reload Shop`）时调：重载通常意味着服主正在动手修（改了盘、改了权限），
/// 这时候再看到一次新的 warn 是有用的——否则旧记录会把同路径的新失败也吃掉。
void resetLogWarnings();

} // namespace ss::shop
