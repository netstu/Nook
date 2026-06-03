# 2026-05-12 Single-Server Spawn Status

## 结论

当前 Android 11 真机稳定默认链已经达到下面这个用户可见状态：

- 设备侧默认只需要一个可见文件：
  - `nook-server`
- `spawn` 可以成功
- `hook` 可以生效
- `libnook-agent.so` 和 `libncore.so` 不再需要作为默认设备部署文件手工推送

这表示 Nook 当前已经在“设备部署表面”上达到了接近 Frida 的单文件形态。

## 当前真实运行形态

这次确认的成功链路不是：

- sidecar `libnook-agent.so`
- sidecar `libncore.so`

而是：

1. `nook-server` 自带 embedded `nook-agent` blob
2. `nook-server` 自带 embedded `ncore` blob
3. `spawn` 默认稳定链仍然使用 `legacy ncore` 语义
4. 但 `ncore` 现在以 embedded blob / memfd 方式被 server 内部带入，不再要求设备上存在可见 `libncore.so`
5. 子进程中的 `nook-agent` 也通过 memfd 路径进入目标进程

所以准确表述应该是：

- `nook-agent`：默认 embedded memfd
- `ncore`：默认 embedded delivery，不再是默认 sidecar 文件
- `spawn`：默认稳定后端仍然是 `legacy ncore` 语义，而不是 `zygote-control` 或 `symbi`

## 本轮已确认的关键日志

真机日志确认了以下时序：

- `embedded agent selected mode=memfd`
- `zygote control disabled; using legacy spawn path`
- `legacy spawn armed pkg=com.ad2001.frida0x1 source=zygote64`
- `AGENT_READY ... stage=1`
- `skip replay control-stage AGENT_READY`
- `AGENT_READY ... stage=0`
- `forward SCRIPT_CREATE`
- `script create ok`
- `script load ok`
- `resume success`
- `lab:frida-0x1:hit:get_random`
- `lab:frida-0x1:hit:check:left=5:right=14`

这说明两阶段 `AGENT_READY`、`spawn gate`、脚本加载、恢复执行、以及 Java hook 已经形成一条稳定闭环。

## 本轮修复点

### 1. embedded agent blob 刷新链修正

修复了 embedded agent blob 生成脚本优先级问题，避免 `nook-server` 内嵌旧版 `libnook-agent.so`。

直接效果：

- 解决了 server 已更新而 child agent 仍然跑旧逻辑的问题
- 修复了 `agent runtime not ready for script create` 这类由新旧协议错配引起的现象

### 2. embedded ncore clear/finalize 句柄生命周期修正

修复了 `ClearSpawnInZygoteEmbedded()` 在清理开始时提前取走 embedded `ncore` handle 的问题。

之前：

- 清理中途失败时
- zygote 内状态未清干净
- host 侧却已经丢失恢复句柄
- 后续容易出现：
  - `clear_spawn_in_zygote failed`
  - `spawn agent-ready timed out`
  - zygote `EOFException`

现在：

- 只有 `aclear + detach` 成功后才真正清掉 handle
- 重试和后续 `spawn` 不会被第一次失败残留拖坏

### 3. 单文件打包表面收敛

更新 `tools/build_single_server_package.ps1`：

- `libnook-agent.so` / `libncore.so` 仍作为本地 build-time artifact 使用
- 仅用于生成 embedded blob
- 最终 `build/single-server-package/...` 默认只保留：
  - `nook-server`

### 4. 清除 memfd agent 的误导错误日志

之前 memfd agent 路径会打印：

- `resolve runtime dir from agent path failed path=/memfd:libnook-agent (deleted)`

这不是功能错误，只是旧路径推断逻辑把 memfd 当成异常。

现在已经改成：

- memfd agent 路径下不再输出该错误日志

## 当前仍然成立的边界

虽然设备部署表面已经是单文件，但架构上还不能说：

- `ncore` 已经被彻底移除

更准确的说法是：

- `ncore` 不再是默认设备侧运行时文件
- 但 `spawn` 的稳定默认后端依旧是 `legacy ncore`

所以后续如果要继续对齐 Frida，下一阶段的问题不再是“怎么少推一个文件”，而是：

- 是否继续让 `legacy ncore` 作为长期稳定主后端存在
- 还是把 `spawn` 主链最终迁移到真正的 agent-owned / zygote-control 模型

## 建议的下一步

按当前状态，后面最合理的顺序是：

1. 先把这次单文件 `spawn` 的 SOP 固化
2. 补一组更明确的回归测试：
   - 重复 `spawn -> hook -> finalize -> spawn`
   - 确认无 sidecar 文件时稳定成立
3. 再评估是否继续推进：
   - `zygote-control`
   - `symbi`
   - 或真正的 agent-owned stable spawn

