#include "mod/LogFile.h"

#include "mod/Shop.h"

#include <fstream>
#include <set>
#include <system_error>

namespace ss::shop {
namespace {

/// 已经提示过的失败种类（`"create_directories"` / `"open"`）。
///
/// spec 要求「写失败只 warn（同进程一次）」：这两种失败一旦发生就是持续性的
/// （目录建不出来、文件打不开），只会每一笔交易都重复一遍同样一句话。
/// 进程级静态状态就够了——模组重载（`/ll reload Shop`）通常意味着服主已经在动手修，
/// 那时候再看一遍新的 warn 是有用的。
std::set<std::string> gWarnedFailures;

void warnOnce(std::string_view kind, std::string const& message) {
    if (!gWarnedFailures.emplace(kind).second) {
        return;
    }
    Shop::getInstance().getSelf().getLogger().warn("{}（同类失败本次运行只提示这一次）", message);
}

} // namespace

std::filesystem::path logDirectory() { return Shop::getInstance().getSelf().getConfigDir() / "logs"; }

bool appendLogLines(
    std::filesystem::path const&    dir,
    std::string const&              fileName,
    std::vector<std::string> const& lines
) {    if (lines.empty()) {
        return true; // 没有东西要写：不算失败，也不必建目录
    }

    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        warnOnce("create_directories", "流水的目录建不出来（" + dir.string() + "）：" + ec.message() + "；这一行丢了");
        return false;
    }

    auto const path = dir / fileName;

    // `binary`：NDJSON 要求 `\n` 结尾，不能让文本模式把它翻成 `\r\n`。
    std::ofstream out(path, std::ios::binary | std::ios::app);
    if (!out) {
        warnOnce("open", "流水文件打不开（" + path.string() + "）；这一行丢了");
        return false;
    }
    for (auto const& line : lines) {
        out << line;
    }
    // flush 之后作用域一结束就关文件：崩服时已成交的流水必须已经在盘上。
    out.flush();
    if (!out) {
        // 磁盘满、盘被拔掉都会走到这里。留一句痕迹，但**不**回头改交易结论。
        warnOnce("write", "流水写入失败（" + path.string() + "）；这一行可能不完整");
        return false;
    }
    return true;
}

} // namespace ss::shop
