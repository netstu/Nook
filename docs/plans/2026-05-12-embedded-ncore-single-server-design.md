# Embedded Ncore Single-Server Design

目标：把当前稳定 `spawn` 默认链从“设备上需要 `nook-server + libncore.so`”收敛为“设备上只需要 `nook-server`”，同时保持当前真机上已经验证可用的 `legacy ncore` 语义和两阶段 `AGENT_READY` 时序。

## 现状

- `attach` 已经默认使用 embedded `nook-agent` memfd 注入。
- `spawn` 当前稳定默认链仍然是 `legacy ncore`，但 `ncore` 已具备 embedded blob + memfd 装载基础：
  - `PrepareSpawnInZygoteEmbedded()`
  - `ClearSpawnInZygoteEmbedded()`
  - `server/generated/nook_embedded_ncore_blob.h`
- 当前设备侧仍默认部署：
  - `nook-server`
  - `libncore.so`

## 目标状态

- 设备侧默认仅部署：
  - `nook-server`
- `spawn` 默认路径：
  1. `nook-server` 将 embedded `ncore` blob 通过 memfd 注入到 zygote
  2. `ncore` 在 zygote 内完成 `ainject/aclear`
  3. 子进程中的 `nook-agent` 继续通过 embedded memfd 路径进入目标进程
- `libncore.so` 不再是默认部署文件
- sidecar `libncore.so` 仅保留为显式 fallback / debug 路径

## 推荐方案

保留 `ncore` 作为独立逻辑单元和独立构建目标，但只把它作为 `nook-server` 的内嵌资产使用，不再把 `libncore.so` 视为默认运行时文件。

原因：

- 改动范围最小，不需要把 `ncore` 逻辑整体并入 server。
- 已有 embedded `ncore` 基础代码可以直接复用。
- 风险明显低于“彻底消灭 `ncore` 并把 fork hook 能力迁移到 agent/zygote-control”。

## 设计边界

### 1. 默认运行路径

- `NinjectorSpawnInjector::SpawnViaLegacyNcore()` 默认优先走 embedded `ncore`
- 不再默认物化或依赖 `/data/local/tmp/nook/libncore.so`
- 若 embedded 路径失败，只有在显式允许时才回退到 sidecar `libncore.so`

### 2. Fallback 策略

- 默认关闭 `libncore.so` sidecar fallback
- 仅在以下场景启用：
  - 显式环境变量
  - 明确的调试模式
  - 后续专门的兼容开关

### 3. 构建与部署

- 构建阶段仍然编出 `libncore.so`
  - 用于生成 `server/generated/nook_embedded_ncore_blob.h`
- 默认部署脚本只推送：
  - `nook-server`
- `libncore.so` 不再属于默认设备侧产物

### 4. 状态与清理

- host 侧继续维护 embedded `ncore` handle
- 只有 `aclear + detach` 成功后才清理 handle
- 重复 `spawn -> finalize -> spawn` 必须稳定，不能再次出现：
  - `clear_spawn_in_zygote failed`
  - zygote `EOFException`
  - 后续 `spawn agent-ready timed out`

## 暂不做的事

- 不在这一轮彻底移除 `ncore`
- 不在这一轮恢复 `zygote-control` 为默认路径
- 不在这一轮改 `symbi` 为默认 `spawn` 后端
- 不在这一轮做 Frida 式 SELinux patch / 真正全链路 fd-only zygote-control

## 验证标准

真机通过以下条件视为本轮完成：

1. `/data/local/tmp/nook` 默认只有 `nook-server`
2. `spawn -f com.ad2001.frida0x1 -l script.js` 成功
3. 重复执行两到三次 `spawn` 仍成功
4. 日志出现：
   - `embedded agent selected mode=memfd`
   - `AGENT_READY ... stage=1`
   - `skip replay control-stage AGENT_READY`
   - `AGENT_READY ... stage=0`
5. 日志不再要求默认存在 `/data/local/tmp/nook/libncore.so`

