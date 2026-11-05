#pragma once

namespace ss::shop {

/// 注册 `/shoptest`：真机自检命令。
///
/// 它不是测试基础设施（没有独立目标、没有框架依赖），而是**在真实服务器上把交易跑一遍**
/// 的手动验收入口。理由见 ADR-0007：本模组唯一的回归保证是真机，而补偿路径
/// （扣钱成功、发货失败 → 退款）没法在引擎外构造。
void registerTestCommand();

} // namespace ss::shop
