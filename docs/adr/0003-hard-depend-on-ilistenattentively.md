# 硬依赖 iListenAttentively，不做降级

事件层几乎全部由 iListenAttentively 提供：告示牌编辑提交、容器打开、攻击方块、爆炸、活塞推动、凋灵破坏、火焰燃烧、双箱配对、货币变更。我们不复制它的 hook 实现。它是 LGPL 的动态库，Shop 链接它的导入表，因此**服务器没装它、或版本过旧时，`Shop.dll` 在 Windows 加载阶段就失败**，不是"少几个功能"。

接受这个代价，因为替代方案是我们在自己代码里维护十来个 LeviLamina 内部符号的 hook——这些符号每次 LeviLamina 大版本都会漂移，而事件语义的维护交给上游更划算。代价还包括：发行声明必须把 iListenAttentively 写成运行时依赖，并且**模组目录里的 `manifest.json` 必须显式声明这条依赖**（LeviLamina 用它排加载顺序；只写在 `tooth.json` 里不够，否则按目录名字母序加载会先加载 Shop 而加载失败，详见 `DESIGN §6.4`），目标服务器的 LeviLamina 下限被它抬到 `>= 26.51.7`。

唯一保留下来的手写 hook 是漏斗抽取拦截：iListenAttentively 没有对应事件，而 `Hopper::_tryPullInItemsFromAboveContainer` / `_pushOutItems` 已被 ChestLock 验证可用。
