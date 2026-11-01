#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace ss::shop {

/// 流水的落盘目录：`plugins/Shop/logs/`（业务流水与销毁留档都在这里）。
///
/// 用 `getConfigDir()`（= 模组的 `plugins/Shop`）拼，别自己写 `plugins/Shop`：
/// 服主怎么装模组由加载器决定。
std::filesystem::path logDirectory();

/// 把一批**已经是完整 NDJSON 行**的文本追加到 `dir / fileName`。
///
/// 每次调用都「建目录 → 开 → 写 → flush → 关」，**不缓存句柄**：文件按天轮转（跨天就是另一个
/// 名字），缓存「当前文件」就要额外维护「跨天了没有」「上次开失败了要不要重开」两种状态，
/// 而这里一天最多写几次，重复开文件的代价可以忽略。
///
/// 失败只 `warn`，而且**同一进程同一种失败只 warn 一次**：流水不是交易的一部分，磁盘满了也
/// 必须让生意成交；但每笔交易刷一条 warn 会把真正的信息淹掉（spec 的决定）。
///
/// @return 这一段是否真的写进去了。调用方**没有**补偿动作（写失败不回滚交易），
///         返回值只用于「写成功之后再顺手做下一件事」这一种判断。
bool appendLogLines(
    std::filesystem::path const&        dir,
    std::string const&                  fileName,
    std::vector<std::string> const&     lines
);

} // namespace ss::shop
