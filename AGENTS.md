## Agent skills

### Issue tracker

Issue 与 spec 以 markdown 文件存在 `.scratch/<feature>/`，不走 GitHub Issues。见 `docs/agents/issue-tracker.md`。

### Triage labels

沿用默认的五个 triage 角色名，外加终态 `resolved`，作为 issue 文件里 `Status:` 行的取值。见 `docs/agents/triage-labels.md`。

### Domain docs

单 context：根目录 `GLOSSARY.md` + `docs/adr/`。见 `docs/agents/domain.md`。

### 找文件

禁止全盘扫描（`find /`、`ls -R /c` 这类）。依赖与头文件位置从 `xmake show`、构建日志、`xmake.lua` 的 `add_requires` 里查，不要猜路径：`mc/` 头文件在 `<xmake 安装目录>/.xmake/packages/l/levilamina/<版本>/<hash>/include/mc/` 下。

`bin/` 是构建产物目录，搜索源码时排除掉（`rg ... --glob '!bin/**'`）。

源码是单层功能目录（ADR-0007）：`src/{event,command,form,core,util}` 加上根目录的
`Shop.h`/`Shop.cpp`/`MemoryOperators.cpp`。`core/` 再分 `sign`/`shop`/`container`/`trade`/`economy`，
`util/` 再分 `config`/`i18n`/`log`/`time`。没有纯逻辑层了，代码可以直接用 `mc/`、`ll/` 类型。

### 文案（i18n）

一切玩家可见文本都走 `util/i18n/`，**不在代码里写中文字面量**：

- `Text` 是文本树（`text::keyed` / `text::lit` / `text::field` / `text::block` …）；
  `Message` 管前缀、`Tone` 与发送（`ui::send` / `ui::line` / `ui::msg`）；
  `Locale` 管装载与渲染（`loadTexts` / `renderText` / `plain`）。
- 新增文案必须同时写进 `lang/zh_CN.json` 与 `lang/en_US.json`，key 形如 `shop.<域>.<用途>`。
- 命令输出也进词表：用 `ui::plain(text::keyed(...))`。别拿「只有管理员看」当理由绕过去。
- 唯一例外是 `Locale.cpp` 里 `loadTexts()` 自己的两条启动日志——词表还没载入时查表只会拿到
  key id，比中文更难读。`static_assert` 的编译期消息不算运行时文案。
