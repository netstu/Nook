# 2026-05-11 spawn 默认后端回归与 zygote install 成功误判

## Summary

这轮真机现象同时暴露了两个问题：

1. `spawn` 默认后端策略发生回归，普通测试流量又被带回了高风险的 `zygote-control -> symbi` 链路。
2. `zygote-control` 路径里，`installForkHook` 的 RPC 成功不能等价为“child specialize hook 已真正生效”。

## User-visible symptom

- `nook-cli -U -f com.ad2001.frida0x1 ...`
- 报错：
  - `spawn agent-ready failed: zygote control failed: inject zygote agent failed: remote_dlopen_failed:dlopen failed: library "/proc/self/fd/58" not found; symbi failed: spawn_symbi failed: spawn_symbi_failed:start_target_app_failed`
- 伴随设备重启或系统服务重启

## Root cause 1: 默认后端策略回归

文件：
- [server/ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_spawn_injector.cpp)

错误语义：

- `NOOK_ENABLE_EXPERIMENTAL_ZYGOTE_CONTROL` 未设置时，`enable_zygote_control = true`
- `NOOK_ALLOW_SYMBI_FALLBACK` 未设置时，`symbi` fallback = true
- `legacy ncore` backend 反而要求额外 env 才能启用

这导致普通 `spawn` 默认顺序又变成：

1. `zygote-control`
2. `symbi`
3. `legacy ncore` 可能根本进不去

而当前目标设备上，前两条都不是稳定主链。

## Root cause 2: `installForkHook success` 具有误导性

真机日志可出现：

- `rpc request handled method=nook.spawn.installForkHook success=1`

但后续完全没有：

- `forkAndSpecialize child matched`
- `nativeForkAndSpecialize child matched`
- `specializeAppProcess child matched`
- `nativeSpecializeAppProcess child matched`
- `selinux_android_setcontext child matched`
- 子进程 `AGENT_READY`

最终仍会落到：

- `authoritative agent ready timeout`

这说明：

- RPC 通道是通的
- 但 zygote 侧真正负责 child 拦截的 hook 可能没装上，或者没覆盖当前 ROM 的真实孵化路径

因此不能把 `installForkHook success` 当作 zygote-control 生效的充分证据。

## Fix

### 默认策略修正

恢复为保守默认：

- 只有 `NOOK_ENABLE_EXPERIMENTAL_ZYGOTE_CONTROL=1` 时才启用 `zygote-control`
- 只有 `NOOK_ALLOW_SYMBI_FALLBACK=1` 时才允许 `symbi` fallback
- `legacy ncore` backend 作为默认稳定保底链路

### 调试判断修正

以后看 `zygote-control`：

- 不能停在 `installForkHook success`
- 必须继续核对 child match / child activation / child `AGENT_READY` 日志

## Why this matters

如果默认策略不收回：

- 普通测试会持续误入高风险链路
- 现象不是单纯超时，而是可能直接污染 zygote / 触发设备重启

如果继续把 install RPC 成功误判成 hook 生效：

- 会把问题错误地归因到 `SessionRegistry` / `pending spawn`
- 导致在服务端超时逻辑上空转，而不是去修真正的 zygote hook 安装链路
