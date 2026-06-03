# 2026-05-12 Spawn Symbi Explicit Flag Design

## 目标

为当前 CLI 增加一个显式实验开关 `--spawn-symbi`，让用户可以手动选择 `symbi` 作为 `spawn` 后端，而不改变当前默认稳定链。

这轮的目标不是把 `symbi` 改成默认，而是：

- 提供一个明确、可测试、可文档化的入口
- 让 `symbi` 与当前稳定默认链并存
- 为后续真机验证和逐步收敛提供 AB 测试能力

## 设计约束

- 这轮只加长选项：
  - `--spawn-symbi`
- 不加短选项：
  - 不加 `-s`
- 不改变默认行为
- 不扩 scope 到 `attach`

## 推荐实现

采用最小侵入方案：

- 不新增协议字段
- 直接把 `--spawn-symbi` 作为一个特殊 argv 标志，通过现有 `SpawnRequest.argv` 从 host 传到 server
- server 在 `NinjectorSpawnInjector::Spawn()` 前提取该标志，并据此改变后端选择顺序

原因：

- 不需要修改 `messages.h/messages.cpp` 和 host/server 协议编解码
- 风险明显低于新增结构化字段
- 更适合作为实验入口

## 行为定义

### 默认行为

不带 `--spawn-symbi` 时：

- 保持当前行为不变
- 默认稳定链仍是当前 embedded `legacy ncore`

### 显式行为

带 `--spawn-symbi` 时：

- 显式请求 `symbi` 作为 `spawn` 首选后端
- server 端优先尝试 `symbi`
- 当前默认 stable backend 不再先于 `symbi` 尝试

## fallback 策略

第一版采用保守策略：

- `--spawn-symbi` 请求时，若 `symbi` 失败
- 仍允许回退到当前稳定默认链
- 但日志和错误信息必须明确说明：
  - 用户请求的是 `symbi`
  - 实际发生了 fallback

这样做的原因：

- 真机调试效率更高
- 不会因为 `symbi` 一次失败就直接阻断整个命令
- 便于逐步收集 `symbi` 稳定性数据

## CLI 覆盖范围

这一轮建议覆盖所有复用 `spawn` helper 的入口：

- `spawn`
- Frida 风格 `-f`
- `call --spawn`
- `post`
- `unload`
- `repl spawn`

原则是：

- 只要底层走 `device.spawn(...)`
- 就应该能显式透传 `--spawn-symbi`

## 测试要求

### CLI 层

- 解析 `--spawn-symbi`
- 标准 `spawn` 和 Frida 风格入口都能保留这个标志

### injector 层

- 默认不带标志时不改变当前顺序
- 带标志时优先走 `symbi`
- `symbi` 成功时，不进入默认 stable legacy prepare
- `symbi` 失败时，若允许 fallback，则进入默认稳定链

## 暂不做的事

- 不新增 `-s`
- 不新增专门的协议字段
- 不把 `symbi` 改成默认
- 不改变 `attach`

