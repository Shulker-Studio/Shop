add_rules("mode.debug", "mode.release")

add_repositories("levimc-repo https://github.com/LiteLDev/xmake-repo.git")

-- Shop 是纯服务端模组
add_requires("levilamina 26.51.8", {configs = {target_type = "server"}})
-- 事件层依赖 iListenAttentively（告示牌编辑、开箱、爆炸/活塞/凋灵/火焰、双箱、货币事件）
add_requires("ilistenattentively", {configs = {target_type = "server"}})
add_requires("levibuildscript")
-- 纯逻辑层的单元测试（header-only），只被 ShopTests 使用，不进入 Shop.dll
add_requires("doctest 2.5.3")
-- JSON：交易流水用 nlohmann::ordered_json 生成（键序固定、转义交给它）。header-only，
-- 纯逻辑层与测试都要它；levilamina 包不传递它，必须单独声明。
add_requires("nlohmann_json 3.12.0")
-- 流水压缩（`p1-transaction-log` D4）：只压缩、永不删除，用 zlib 的 `gzopen`/`gzwrite` 流式写。
-- 只有 BDS 适配层用（纯逻辑层只算「该不该压」），所以只有 Shop 链接它。
add_requires("zlib 1.3.2")

if not has_config("vs_runtime") then
    set_runtimes("MD")
end

-- 纯逻辑模块（解析器、计价）不允许依赖引擎：源码里出现 mc/ 头就让构建直接失败。
-- 这是 DESIGN「纯逻辑不得依赖引擎」的构建期断言，Shop 与 ShopTests 都会调它。
function check_pure_logic_headers()
    -- 注意：os.files 的 `|` 交替写法只匹配到第一种，所以分开扫再拼起来
    -- 三类都禁：mc/ 是引擎、ll/ 是 LeviLamina、mod/ 是本模组的适配层。
    -- 只查直接包含：传递包含需要真的过一遍预处理，那是构建期而不是脚本能干的。
    local forbidden = {
        ['mc/'] = '[#]%s*include%s*[<"]mc/',
        ['ll/'] = '[#]%s*include%s*[<"]ll/',
        ['mod/'] = '[#]%s*include%s*[<"]mod/',
    }
    local files = table.join(os.files("src/shop/**.h"), os.files("src/shop/**.cpp"))
    for _, file in ipairs(files) do
        local content = io.readfile(file) or ""
        for name, pattern in pairs(forbidden) do
            if content:find(pattern) then
                raise("纯逻辑模块不允许包含 " .. name .. " 头：" .. file)
            end
        end
    end
end

target("Shop")
    add_rules("@levibuildscript/linkrule")
    -- 版本号与 tooth.json 保持一致；发布时同步修改两处
    add_rules("@levibuildscript/modpacker", {modVersion = "0.1.0"})
    if is_plat("windows") then
        add_defines("NOMINMAX", "UNICODE")
        set_exceptions("none") -- To avoid conflicts with /EHa.
        add_cxflags( "/EHa", "/utf-8", "/W4", "/w44265", "/w44289", "/w44296", "/w45263", "/w44738", "/w45204")
        add_cxflags(
            "/EHs",
            "-Wno-microsoft-cast",
            "-Wno-invalid-offsetof",
            "-Wno-c++2b-extensions",
            "-Wno-microsoft-include",
            "-Wno-overloaded-virtual",
            "-Wno-ignored-qualifiers",
            "-Wno-missing-field-initializers",
            "-Wno-potentially-evaluated-expression",
            "-Wno-pragma-system-header-outside-header",
            {tools = {"clang_cl"}}
        )
        set_toolchains("clang-cl")
    end
    add_packages("levilamina", "ilistenattentively", "nlohmann_json", "zlib")
    set_kind("shared")
    set_languages("c++20")
    set_symbols("debug")
    add_headerfiles("src/**.h")
    add_files("src/**.cpp")
    add_includedirs("src")
    before_build(check_pure_logic_headers)

    -- `modpacker` 只搬 dll / pdb / manifest.json：语言文件得自己跟进产物目录，
    -- 否则发出去的包里没有 `lang/`，所有玩家文本都会显示成 key id。
    after_build(function(target)
        if not os.isdir("lang") then
            return
        end
        local outdir = path.join(os.projectdir(), "bin", target:name(), "lang")
        os.mkdir(outdir)
        for _, file in ipairs(os.files("lang/*.json")) do
            os.cp(file, path.join(outdir, path.filename(file)))
        end
    end)

-- 纯逻辑层的测试：只编译 src/shop 与 tests，不链接 levilamina，因此没有 BDS 也能跑。
-- 用法：xmake test
target("ShopTests")
    set_default(false)
    set_kind("binary")
    set_languages("c++20")
    if is_plat("windows") then
        -- 与 Shop 一致：LeviLamina 26.20 之后用 clang-cl，不用 msvc
        set_toolchains("clang-cl")
        add_cxflags("/EHa", "/utf-8")
    end
    add_packages("doctest", "nlohmann_json")
    add_includedirs("src")
    add_files("src/shop/**.cpp", "tests/**.cpp")
    -- 测试从 `lang/zh_CN.json` 取文本，所以运行目录必须是项目根。
    set_rundir(os.projectdir())
    add_tests("default")
    before_build(check_pure_logic_headers)
