# 更新日志

本项目所有值得记录的变更都会写在这里。

格式遵循 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)，
版本号遵循 [语义化版本](https://semver.org/lang/zh-CN/)。

## [Unreleased]

### 新增

- 基于 LeviLamina 模组模板初始化工程：target `Shop`，命名空间 `ss::shop`，类 `Shop`。
- 依赖 LeviLamina `26.51.8` 与 iListenAttentively `0.17.0`（事件层）。
- `DESIGN.md`：完整设计文档（数据模型、事件与 hook、交易流程、商店箱保护、经济契约、决策记录）。
- `README.md`：中文项目说明与构建/安装指引。
- `tooth.json`：填充仓库地址、运行时依赖与发布产物路径。

### 变更

- `xmake.lua`：移除 server/client 双目标，固定为服务端模组；版本号由 `modpacker` 显式指定为 `0.1.0`。
- `manifest.json`：移除未被替换的 `platform` 占位符。
- CI：`build.yml` / `release.yml` 只构建服务端产物。
