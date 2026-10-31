# Issue tracker：本地 Markdown

本仓库的 issue 与 spec 以 markdown 文件存放在 `.scratch/`。

## 约定

- 一个功能一个目录：`.scratch/<feature-slug>/`
- spec 是 `.scratch/<feature-slug>/spec.md`
- 实施 issue 一个 ticket 一个文件：`.scratch/<feature-slug>/issues/<NN>-<slug>.md`，从 `01` 编号，永远不要写成单个合并的 tickets 文件
- triage 状态记录在每个 issue 文件靠顶部的 `Status:` 行（角色字符串见 `triage-labels.md`）
- 评论与对话历史追加到文件底部的 `## Comments` 标题下

## 当某个 skill 说「发布到 issue tracker」时

在 `.scratch/<feature-slug>/` 下新建文件（目录不存在就创建）。

## 当某个 skill 说「取出相关 ticket」时

读引用的那个文件。用户通常会直接给出路径或 issue 编号。

## Wayfinding 操作

供 `/wayfinder` 使用。**map** 是一个文件，每个 ticket 对应一个 **child** 文件。

- **Map**：`.scratch/<effort>/map.md`（承载 Notes / Decisions-so-far / Fog 正文）。
- **Child ticket**：`.scratch/<effort>/issues/NN-<slug>.md`，从 `01` 编号，正文写问题。`Type:` 行记录 ticket 类型（`research`/`prototype`/`grilling`/`task`）；`Status:` 行记录 `claimed`/`resolved`。
- **Blocking**：靠顶部的 `Blocked by: NN, NN` 行。它列出的每个文件都是 `resolved` 时，该 ticket 才算解除阻塞。
- **Frontier**：扫描 `.scratch/<effort>/issues/`，取 open、未阻塞、未被认领的文件；编号最小的优先。
- **Claim**：动工前先设 `Status: claimed` 并保存。
- **Resolve**：在 `## Answer` 标题下追加答案，设 `Status: resolved`，再往 `map.md` 的 Decisions-so-far 追加一条上下文指针（摘要 + 链接）。
