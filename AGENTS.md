## Agent skills

### Issue tracker

Issue 与 spec 以 markdown 文件存在 `.scratch/<feature>/`，不走 GitHub Issues。见 `docs/agents/issue-tracker.md`。

### Triage labels

沿用默认的五个 triage 角色名，外加终态 `resolved`，作为 issue 文件里 `Status:` 行的取值。见 `docs/agents/triage-labels.md`。

### Domain docs

单 context：根目录 `GLOSSARY.md` + `docs/adr/`。见 `docs/agents/domain.md`。

### 找文件

禁止全盘扫描（`find /`、`ls -R /c` 这类）。依赖与头文件位置从 `xmake show`、构建日志、`xmake.lua` 的 `add_requires` 里查，不要猜路径：`mc/` 头文件在 `<xmake 安装目录>/.xmake/packages/l/levilamina/<版本>/<hash>/include/` 下（本机实例：`26.51.8/88cc02476b934dabad6469c6cf5d695d/include/mc/`）。

`bin/` 是构建产物目录，搜索源码时排除掉（`rg ... --glob '!bin/**'`）；`src/shop/` 是纯逻辑层，不允许出现 `mc/` 头文件，`xmake` 的 `check_pure_logic_headers()` 会在构建时检查。
