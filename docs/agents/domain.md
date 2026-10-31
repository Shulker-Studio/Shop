# Domain 文档

工程类 skill 在探索代码库时应当如何消费本仓库的 domain 文档。

## 探索之前先读这些

- 根目录的 **`GLOSSARY.md`**，或
- 根目录的 **`GLOSSARY-MAP.md`**（如果存在）：它指向每个 context 各自的 `GLOSSARY.md`，读其中与当前话题相关的那些。
- **`docs/adr/`**：读与即将动手的区域相关的 ADR。多 context 仓库还要看 `src/<context>/docs/adr/` 里 context 级的决策。

如果这些文件不存在，**静默略过**。不要指出它们缺失，也不要在开工前提议创建。`/domain-modeling` skill（经由 `/grill-with-docs` 和 `/improve-codebase-architecture` 抵达）会在术语或决策真正被确定时按需创建它们。

## 文件结构

单 context 仓库（大多数仓库）：

```
/
├── GLOSSARY.md
├── docs/adr/
│   ├── 0001-event-sourced-orders.md
│   └── 0002-postgres-for-write-model.md
└── src/
```

多 context 仓库（根目录存在 `GLOSSARY-MAP.md`）：

```
/
├── GLOSSARY-MAP.md
├── docs/adr/                          ← 全局决策
└── src/
    ├── ordering/
    │   ├── GLOSSARY.md
    │   └── docs/adr/                  ← context 专属决策
    └── billing/
        ├── GLOSSARY.md
        └── docs/adr/
```

## 使用 glossary 的词汇

当你的产出提到某个 domain 概念（issue 标题、重构提议、假设、测试名）时，用 `GLOSSARY.md` 里定义的术语。不要漂移到 glossary 明确避免的同义词。

如果你需要的概念还不在 glossary 里，那是个信号：要么你在发明项目没在用的词（重新考虑），要么这里确实有缺口（记下来交给 `/domain-modeling`）。

## 标注 ADR 冲突

如果你的产出与已有 ADR 矛盾，显式提出来，不要默默覆盖：

> _与 ADR-0007（事件溯源订单）冲突，但值得重开，因为……_
