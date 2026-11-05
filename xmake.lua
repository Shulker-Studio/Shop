add_rules("mode.debug", "mode.release")

add_repositories("levimc-repo https://github.com/LiteLDev/xmake-repo.git")

-- Shop 是纯服务端模组
add_requires("levilamina 26.60.1", {configs = {target_type = "server"}})
-- 事件层依赖 iListenAttentively（告示牌编辑、开箱、爆炸/活塞/凋灵/火焰、双箱、货币事件）
add_requires("ilistenattentively 0.18.0", {configs = {target_type = "server"}})
add_requires("levibuildscript")
-- JSON：交易流水用 nlohmann::ordered_json 生成（键序固定、转义交给它）。header-only，
-- levilamina 包不传递它，必须单独声明。
add_requires("nlohmann_json 3.12.0")
-- 流水压缩（`p1-transaction-log` D4）：只压缩、永不删除，用 zlib 的 `gzopen`/`gzwrite` 流式写。
-- 只有引擎侧用，所以只有 Shop 链接它。
add_requires("zlib 1.3.2")

if not has_config("vs_runtime") then
    set_runtimes("MD")
end

target("Shop")
    add_rules("@levibuildscript/linkrule")
    -- 版本号的唯一来源是 `tooth.json`（与 STweaks / LK-Stats 同款做法）：tooth 的 `version`
    -- 决定 lip 包与 release tag（`v{{version}}`）；构建出的 `manifest.json` 再带上
    -- `+<短 sha>`，同版本的不同构建在 `/shop status` 里分得开（`ll::data::Version` 认 build metadata）。
    -- 不写死 modVersion：那样改版本要改两处；也不靠 `git describe`：本地没 tag 时会退成 0.0.0。
    on_load(function(target)
        import("core.base.json")
        local modVersion = json.loadfile("tooth.json")["version"]
        local major, minor, patch = modVersion:match("^(%d+)%.(%d+)%.(%d+)$")
        if not major then
            raise("tooth.json 的 version 必须是 major.minor.patch：" .. tostring(modVersion))
        end
        local buildMetadata = os.iorun("git rev-parse --short HEAD"):gsub("\n", "")
        target:set("version", modVersion)
        target:add("rules", "@levibuildscript/modpacker", {modVersion = modVersion .. "+" .. buildMetadata})
    end)
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
