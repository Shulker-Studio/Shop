# Triage 标签

skill 用五个规范 triage 角色来表述。本文件把这些角色映射到本仓库 issue tracker 里实际使用的标签字符串。

| mattpocock/skills 中的标签 | 本仓库跟踪器中的标签 | 含义 |
| --- | --- | --- |
| `needs-triage` | `needs-triage` | 维护者需要评估这个 issue |
| `needs-info` | `needs-info` | 等待报告者补充信息 |
| `ready-for-agent` | `ready-for-agent` | 已完整描述，可交给挂机 agent |
| `ready-for-human` | `ready-for-human` | 需要人类实现 |
| `wontfix` | `wontfix` | 不予处理 |
| `resolved` | `resolved` | 已实现并有回归保护，等人类验收或合并（终态，实现类 ticket 用） |

前五个是 triage 角色，`resolved` 是**终态**：实现完成时把 `Status:` 改成它，并在文件末尾追加 `## Answer` 写实现摘要与验证命令。wayfinder 的 child ticket 也用 `claimed`/`resolved`，含义相同。

当某个 skill 提到某个角色（例如「打上 AFK-ready 的 triage 标签」）时，使用本表中对应的标签字符串。

本仓库用本地 markdown tracker，这些标签写在 issue 文件顶部的 `Status:` 行。

要换自己的命名，改右列即可。
