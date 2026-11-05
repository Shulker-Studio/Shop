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

当前源码还是 `src/shop`（纯逻辑）+ `src/mod`（引擎侧）两层；ADR-0007 定的目标结构是单层
`src/{event,command,form,core,policy,util}`，不再要求纯逻辑层不碰 `mc/` 头（测试与断言一并删除）。
改目录前按两层找，改后按功能目录找。
