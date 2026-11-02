#include "mod/LogFile.h"

#include "mod/Shop.h"
#include "shop/LogRotation.h"

#include "ll/api/io/Logger.h"

#include <zlib.h>

#include <fstream>
#include <set>
#include <string>
#include <system_error>
#include <vector>

namespace ss::shop {
namespace {

/// 已经提示过的失败种类（`"create_directories"` / `"open"` / `"write"` / `"compress"`）。
///
/// spec 要求「写失败只 warn（同进程一次）」：这几种失败一旦发生就是持续性的
/// （目录建不出来、文件打不开、磁盘仍是满的），只会每一笔交易都重复一遍同样一句话。
/// 进程级静态状态就够了——模组重载（`/ll reload Shop`）通常意味着服主已经在动手修，
/// 那时候再看一遍新的 warn 是有用的。
std::set<std::string> gWarnedFailures;

void warnOnce(std::string_view kind, std::string const& message) {
    if (!gWarnedFailures.emplace(kind).second) {
        return;
    }
    Shop::getInstance().getSelf().getLogger().warn("{}（同类失败本次运行只提示这一次）", message);
}

/// 压缩用的缓冲。64 KiB：足够摊平每次 `gzwrite` 的调用开销，又不至于为一条日志占多少内存。
constexpr std::size_t kCopyBuffer = 64 * 1024;

/// 把一个文件 gzip 成 `finalPath`，**流式**读写（spec D4：不把整个文件读进内存——`discarded`
/// 里可能有几十 KB 的 SNBT，一天的流水可以到几十 MB）。
///
/// 顺序是「写 `.part` → 关好 → 改名为成品 → 删原文件」，每一步都为了同一件事：
/// **成品名只会在内容完整时出现**。
/// - 直接写成品名的话，中途崩服会留下一个截断的 `.gz`，下一轮看到「`.gz` 已存在」就当它压好了，
///   然后删掉原文件——这就是拿数据换磁盘；
/// - 改名前失败只留一个 `.part`（`logFileDate` 认不出它，扫描时天然跳过），下一轮重写；
/// - 删原文件放在最后，删不掉也只是留着两份，`compressLogFile` 下一轮补删。
///
/// @return 是否成功（成功 = `.gz` 写完并关好、已改名、原文件已删）。
bool gzipFile(std::filesystem::path const& source, std::filesystem::path const& finalPath) {
    auto const target = std::filesystem::path{finalPath.string() + ".part"};

    // 两个句柄都在这个作用域里，**出作用域先关掉**再删原文件：Windows 上不共享删除权限，
    // 自己还开着 `ifstream` 就删不掉——压缩会永远停在「`.gz` 写好了、原文件还在」。
    bool ok = true;
    {
        std::ifstream in(source, std::ios::binary);
        if (!in) {
            return false;
        }

#if defined(_WIN32)
        // Windows 上 `gzopen` 收窄字符串（系统代码页）：服主的目录名里有中文时就会打不开，
        // 所以走宽字符版本。别的平台用普通版本。
        gzFile out = gzopen_w(target.c_str(), "wb");
#else
        gzFile out = gzopen(target.string().c_str(), "wb");
#endif
        if (out == nullptr) {
            return false;
        }

        std::vector<char> buffer(kCopyBuffer);
        while (in) {
            in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            auto const read = in.gcount();
            if (read <= 0) {
                break;
            }
            // `gzwrite` 要的是 `unsigned` 长度；64 KiB 的块永远放得下。
            if (gzwrite(out, buffer.data(), static_cast<unsigned>(read)) != static_cast<int>(read)) {
                ok = false;
                break;
            }
        }
        if (in.bad()) {
            ok = false; // 读到一半出错：别把半个文件当成完整的
        }
        // `gzclose` 的返回值不能丢：压缩流的校验和与小尾巴是在它里面写出去的。
        if (gzclose(out) != Z_OK) {
            ok = false;
        }
    }
    std::error_code ec;
    if (!ok) {
        // 半成品不能留着：留着会让下一轮以为这一份已经压过。
        std::error_code cleanup;
        std::filesystem::remove(target, cleanup);
        return false;
    }

    std::filesystem::rename(target, finalPath, ec);
    if (ec) {
        std::error_code cleanup;
        std::filesystem::remove(target, cleanup);
        return false;
    }

    std::filesystem::remove(source, ec);
    // 删原文件失败也算失败：留着两个版本会让人以为压缩没生效（下一轮补删）。
    return !ec;
}

/// 压一个 `dir / fileName`。幂等：成品（`.jsonl.gz`）已经存在时只补「删原文件」这一步。
void compressLogFile(std::filesystem::path const& dir, std::string const& fileName) {
    auto const source = dir / fileName;
    auto const target = dir / compressedLogFileName(fileName);

    std::error_code ec;
    if (std::filesystem::exists(target, ec)) {
        // 成品在 = 上一次已经压成功（`.part` 只做中间态，从不会被当成成品），
        // 只是原文件没删掉（上一轮删的时候自己还开着，或者盘被拔了）。
        // 补上这一步，而不是永远留着两份——否则「压缩省磁盘」就成了空话。
        std::filesystem::remove(source, ec);
        if (ec) {
            warnOnce("compress", "旧日志删不掉（" + source.string() + "）：" + ec.message() + "；已压好的 .gz 保留");
        }
        return;
    }

    if (!gzipFile(source, target)) {
        // 失败不删原文件（`gzipFile` 保证），下一次写日志时还会扫到它，重试是幂等的。
        warnOnce("compress", "日志压缩失败（" + source.string() + "）；原文件保留，下一轮会再试");
    }
}

/// 把 `dir` 里过期的 `.jsonl` 压掉（`transactions-*` 与 `discarded-*` 一视同仁——
/// 它们共用 `logFileDate` 的命名规则，spec D5 要求同一套轮转）。
///
/// 每一轮都真的扫一遍目录，不做「一天只扫一次」的节流：节流会让**压缩失败**的文件一直等到
/// 第二天才重试，而 spec 要的是「下一轮再试」。目录里最多几十个文件，写一次日志扫一遍的代价
/// 可以忽略。
void compressOldLogs(std::filesystem::path const& dir, std::string const& today, int compressAfterDays) {
    if (compressAfterDays <= 0) {
        return; // 0 = 永不压缩：连目录都不必看
    }

    std::error_code                     ec;
    std::filesystem::directory_iterator it(dir, ec);
    if (ec) {
        return; // 目录还不存在（第一次写之前）：没什么可压的，也不是失败
    }
    for (auto const& entry : it) {
        if (entry.is_directory(ec) || ec) {
            continue; // 只认自己的日志文件；子目录不碰
        }
        auto const name = entry.path().filename().string();
        auto const date = logFileDate(name);
        if (!date || !shouldCompress(*date, today, compressAfterDays)) {
            continue;
        }
        compressLogFile(dir, name);
    }
}

} // namespace

std::filesystem::path logDirectory() {
    // `getModDir()` = `plugins/<模组名>`，与配置目录（`plugins/<模组名>/config`）平级。
    return Shop::getInstance().getSelf().getModDir() / "logs";
}

bool appendLogLines(
    std::filesystem::path const&    dir,
    std::string const&              fileName,
    std::vector<std::string> const& lines
) {
    if (lines.empty()) {
        return true; // 没有东西要写：不算失败，也不必建目录
    }

    auto const* config = currentShopConfig();
    if (config == nullptr || !config->transactionLog.enabled) {
        // 总开关：两个文件一起停（业务流水与 `discarded` 都从这一个函数出去）。
        // 配置读不到（模组没启用）也当作关着——没有配置的时候没有「日志策略」可言。
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        warnOnce("create_directories", "日志目录建不出来（" + dir.string() + "）：" + ec.message() + "；这一行丢了");
        return false;
    }

    auto const path = dir / fileName;

    // `binary`：NDJSON 要求 `\n` 结尾，不能让文本模式把它翻成 `\r\n`。
    std::ofstream out(path, std::ios::binary | std::ios::app);
    if (!out) {
        warnOnce("open", "日志文件打不开（" + path.string() + "）；这一行丢了");
        return false;
    }
    for (auto const& line : lines) {
        out << line;
    }
    // flush 之后作用域一结束就关文件：崩服时已成交的流水必须已经在盘上。
    out.flush();
    if (!out) {
        // 磁盘满、盘被拔掉都会走到这里。留一句痕迹，但**不**回头改交易结论。
        warnOnce("write", "日志写入失败（" + path.string() + "）；这一行可能不完整");
        return false;
    }

    // 写完顺手看一眼轮转（spec D4：不额外开线程、不做定时任务）。
    // 「今天」直接来自刚写的这个文件名——它的日期就是今天，不必再读一次时钟。
    if (auto const today = logFileDate(fileName)) {
        compressOldLogs(dir, *today, config->transactionLog.compressAfterDays);
    }
    return true;
}

} // namespace ss::shop
