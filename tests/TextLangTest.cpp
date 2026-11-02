#include <doctest/doctest.h>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

namespace {

/// 词表里的一条：键 → 值。
using Table = nlohmann::json;

Table loadTable(char const* path) {
    std::ifstream in{path};
    REQUIRE_MESSAGE(in.good(), "打不开词表", path);
    Table table;
    in >> table;
    REQUIRE_MESSAGE(table.is_object(), "词表顶层必须是对象", path);
    return table;
}

std::set<std::string> keysOf(Table const& table) {
    std::set<std::string> keys;
    for (auto it = table.begin(); it != table.end(); ++it) {
        keys.emplace(it.key());
    }
    return keys;
}

/// 模板里 `{}` 的个数。漏一个占位符的后果不是编译错误，而是运行时
/// 直接被渲染器判成 `<bad-args:key>`——所以在这里挡住它。
std::size_t slotCount(std::string const& text) {
    std::size_t count = 0;
    for (std::size_t i = 0; i + 1 < text.size(); ++i) {
        if (text[i] == '{' && text[i + 1] == '}') {
            ++count;
            ++i;
        }
    }
    return count;
}

} // namespace

TEST_CASE("词表: zh_CN 与 en_US 的 key 集合必须完全一致") {
    auto const zh = keysOf(loadTable("lang/zh_CN.json"));
    auto const en = keysOf(loadTable("lang/en_US.json"));

    // 少一条 = 那种语言下玩家会看到 key id；多一条 = 有人改了 key 却没清旧条目。
    for (auto const& key : zh) {
        CHECK_MESSAGE(en.count(key) == 1, key);
    }
    for (auto const& key : en) {
        CHECK_MESSAGE(zh.count(key) == 1, key);
    }
}

TEST_CASE("词表: 同一 key 在中英两份里的 {} 占位符个数必须相同") {
    auto const zh = loadTable("lang/zh_CN.json");
    auto const en = loadTable("lang/en_US.json");

    for (auto it = zh.begin(); it != zh.end(); ++it) {
        auto const& key = it.key();
        if (!en.contains(key)) {
            continue; // 上面那条用例已经报过
        }
        auto const& zhText = it.value().get_ref<std::string const&>();
        auto const& enText = en[key].get_ref<std::string const&>();
        CHECK_MESSAGE(slotCount(zhText) == slotCount(enText), key);
    }
}

namespace {

/// 扫一份源码，把 `"shop.xxx.yyy"` 形式的字符串字面量收进来。
///
/// **这不是完整的 C++ 词法分析**，只是抓引号对之间的纯 ASCII `shop.*` 片段：注释里的
/// 同名 key 会被一起收进来（只会多报，不会漏报），而**运行时拼出来的 key**
/// （`"shop.log." + area`）扫不到——那种写法要么改成显式字面量，要么靠人工审查。
/// Shop 目前所有 key 都是整条字面量，这条用例才有意义。
void collectKeys(std::filesystem::path const& file, std::set<std::string>& keys) {
    std::ifstream  in{file};
    std::string    source{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    std::size_t    i = 0;
    while ((i = source.find('"', i)) != std::string::npos) {
        std::size_t const begin = ++i;
        while (i < source.size() && source[i] != '"') {
            if (source[i] == '\\') {
                ++i;
            }
            ++i;
        }
        if (i >= source.size()) {
            break;
        }
        std::string candidate = source.substr(begin, i - begin);
        ++i;
        if (candidate.rfind("shop.", 0) != 0) {
            continue;
        }
        bool ascii = true;
        for (char const c : candidate) {
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.'
                  || c == '_')) {
                ascii = false;
                break;
            }
        }
        if (ascii) {
            keys.emplace(std::move(candidate));
        }
    }
}

std::set<std::string> keysUsedInSources() {
    std::set<std::string> keys;
    std::error_code       ec;
    for (auto const& entry : std::filesystem::recursive_directory_iterator{"src", ec}) {
        if (!entry.is_regular_file()) {
            continue;
        }
        auto const ext = entry.path().extension();
        if (ext == ".cpp" || ext == ".h") {
            collectKeys(entry.path(), keys);
        }
    }
    return keys;
}

} // namespace

/// 这条用例是给「加了一句新文案但忘了写词表」兜底的：`ui::msg(key)` 查不到会
/// 原样输出 key id，而那条路径（日志、命令输出）没有被严格的测试渲染器覆盖。
TEST_CASE("词表: 源码里引用的每个 shop.* key 都必须在 zh_CN.json 里") {
    auto const used = keysUsedInSources();
    auto const zh   = keysOf(loadTable("lang/zh_CN.json"));

    REQUIRE_MESSAGE(!used.empty(), "一个 key 都没扫到，说明扫描路径或 rundir 不对");
    for (auto const& key : used) {
        CHECK_MESSAGE(zh.count(key) == 1, key);
    }
}
