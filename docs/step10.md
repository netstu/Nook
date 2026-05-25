# Step 10: 单文件部署 + Hook 引擎对齐 Frida

## 背景

Nook 当前需要多个文件部署：`nook-server`、`libnook-agent.so`、`libncore.so`、`libc++_shared.so`、`spawn_markers/`。
Frida 只需一个 `frida-server`。

根本原因不是 Frida 不需要 agent/helper/runtime，而是它把这些全部内化了：

- **frida-helper** 静态链接进 frida-server — ptrace 注入能力编译在 server 内部
- **frida-agent** 嵌入 server，通过 memfd_create 注入 — `Linjector.inject_library_fd(pid, memfd, ...)` 从内存注入，不落盘
- **frida-agent 自身实现 ForkHandler/SpawnHandler** — 不需要独立的 .so 来安装 fork hook

Nook 的对齐路径：消除每一个外部依赖，最终做到 `adb push nook-server && ./nook-server`。

## 已完成

| 阶段 | 任务 | 状态 |
|------|------|------|
| Phase A/B | server/agent 静态链接 C++ runtime，消除 `libc++_shared.so` | ✅ 已完成 |
| P1 | Agent 嵌入 Server（blob + 自动释放） | ✅ 已完成 |

P1 完成后部署从 4 个文件缩减到 2 个：`nook-server`（内嵌 agent blob）+ `libncore.so`。

---

## P2: 消除 libncore.so — 对齐 Frida 架构

### Nook vs Frida 的 Spawn 架构对比

```
Nook 当前 (两个组件，分离设计):
  Server --ptrace--> Zygote --dlopen--> libncore.so
                                          ↓
                                        ainject(pkg, agent_path)
                                        安装 fork hook
                                          ↓
                              Zygote forks → Child → ncore 安排 dlopen(agent.so)

Frida (单组件，一体化):
  Server --ptrace--> Zygote --memfd/dlopen--> frida-agent.so
                                                ↓
                                              agent 自身注册 ForkHandler
                                                ↓
                              Zygote forks → Child → agent 已在内存中(fork 继承)
                                                     ForkHandler 激活 → 连回 server
```

Frida 不分离"注入基础设施"和"agent runtime"——agent 自己就是 fork handler。
Nook 的正确做法不是"嵌入 ncore"，而是**消灭 ncore**，把 fork hook 能力并入 agent。

### Nook Agent 现状

Agent (`NookComm.cpp`) 已具备大量 Zygote 感知能力，合并 ncore 的工作量比想象的小：

| 能力 | 现状 | 说明 |
|------|------|------|
| Zygote 进程检测 | ✅ 已有 | `LooksLikeEarlySpawnProcessName()` 识别 zygote/zygote64/usap |
| 延迟激活 | ✅ 已有 | `DeferredAgentActivationThreadMain()` 25ms 轮询进程名变化 |
| Spawn marker 检测 | ✅ 已有 | `ResolveAgentProcessNameForInit()` 读取 marker 文件 |
| Spawn gate | ✅ 已有 | Hook `Instrumentation.callApplicationOnCreate` 阻塞 app 启动 |
| 回调上报 | ✅ 已有 | `ReportSpawnGateReadyIfNeeded()` 写 spawn_result.json |
| 连接 server | ✅ 已有 | Unix socket + AGENT_READY |
| **fork hook** | ❌ 缺失 | 当前由 ncore 的 `ainject()` 在 Zygote 内安装 |

**唯一缺的就是 fork hook 本身。**

### 目标流程

```
Server --ptrace--> Zygote --dlopen--> libnook-agent.so
                                        ↓
                                      构造函数触发
                                      检测到 Zygote → 进入 Zygote 模式
                                      安装 fork/specialize hook
                                        ↓
                            Zygote forks → Child
                                        ↓
                                      agent 已在内存中 (fork 继承)
                                      specialize hook 触发
                                      检测 spawn marker → 匹配目标 pkg?
                                        ↓
                                      是 → 激活完整运行时，连回 server，arm spawn gate
                                      否 → 静默，不激活 (零开销)
```

### M1: Agent Zygote 模式 — fork/specialize hook (3-4 天)

**目标**：Agent 在 Zygote 中安装 fork hook，替代 ncore 的 `ainject()`

**hook 点选择**（三个候选，按优先级）：

```
选项 A (推荐): hook nativeForkAndSpecialize / nativeSpecializeAppProcess
  - 这是 Zygote 的 Java native 方法，通过 Nook 已有的 Java Hook 能力直接 hook
  - 优点：复用现有 Java Hook 基础设施，不需要额外的 native hook
  - Frida 也是这个层级介入

选项 B: hook fork() / _Fork() 系统调用
  - PLT hook libart.so 或 libc.so 的 fork
  - 优点：更底层，更通用
  - 缺点：fork 返回时还不知道包名，需要额外逻辑判断

选项 C: hook SpecializeCommon (ART 内部)
  - Inline hook ART 的 C++ 内部函数
  - 优点：精确控制
  - 缺点：ART 版本碎片化，签名不稳定
```

**推荐选项 A**，具体实现：

```cpp
// src/framework/nook_zygote_handler.h/.cpp

class ZygoteHandler {
public:
    // Server 通过 agent 消息通道调用
    static bool Install(const std::string& target_package,
                        const std::string& spawn_marker_dir);
    static bool Uninstall();

private:
    // Java Hook: com.android.internal.os.Zygote.nativeForkAndSpecialize
    // 或 com.android.internal.os.ZygoteCommandBuffer.nativeForkRepeatedly (Android 12+)
    static void OnPostFork(int pid, bool is_child);

    // 子进程中: 检查是否为目标 app
    static void OnChildSpecialized();

    static std::string target_package_;
    static bool installed_;
};
```

**关键流程**：

```cpp
void ZygoteHandler::OnPostFork(int pid, bool is_child) {
    if (!is_child) return;  // 父进程 (Zygote) 不处理

    // 子进程中: agent 已通过 fork 继承到内存
    // 此时 /proc/self/cmdline 尚未更新为 app 包名
    // 但 nativeForkAndSpecialize 的参数包含 uid/gid/packageName
    // 可以直接从参数判断是否为目标 app

    if (IsTargetPackage(args)) {
        // 激活完整 agent 运行时
        ActivateInChild();
    }
    // 非目标 app: 什么都不做，agent 代码在内存中但不执行
    // (fork 继承的 .so 不能 dlclose，但不激活则零运行时开销)
}
```

**Android 版本适配**：

| Android 版本 | Hook 目标 | 说明 |
|-------------|-----------|------|
| 8.0-11 | `Zygote.nativeForkAndSpecialize` | 标准路径 |
| 12+ | `ZygoteCommandBuffer.nativeForkRepeatedly` | 新增的批量 fork 路径，需同时 hook |
| 12+ | `Zygote.nativeSpecializeAppProcess` | 用于 USAP 模式 |

### M2: Server spawn 流程改造 (2-3 天)

**目标**：Server 的 spawn 流程改为"注入 agent 到 Zygote + 通过消息通道指示安装 fork hook"

**当前 spawn 流程** (`NinjectorSpawnInjector::Spawn()`):
```
1. get_pid("zygote64")
2. prepare_spawn(zygote_pid, ncore_path, pkg, agent_path)  ← 注入 ncore
3. CreateSpawnMarker(pkg)
4. start_target_app(pkg)
5. wait_for_callback(callback_file)
6. clear_spawn(zygote_pid, ncore_path)  ← 清理 ncore hook
```

**新 spawn 流程**:
```
1. get_pid("zygote64")
2. inject_agent_if_needed(zygote_pid, agent_path)  ← 直接注入 agent
3. send_install_fork_hook(zygote_pid, pkg)          ← 通过消息通道
4. CreateSpawnMarker(pkg)
5. start_target_app(pkg)
6. wait_for_agent_ready(pid)                        ← 等 AGENT_READY
7. send_uninstall_fork_hook(zygote_pid)             ← 清理
```

**核心改动**：
- 新增 `AgentSpawnInjector` 替代 `NinjectorSpawnInjector`（或在现有 injector 中增加模式）
- 复用现有 `InjectSoByPid()` 将 agent 注入 Zygote
- 通过 agent 的通信通道（Unix socket）发送 fork hook 安装/卸载指令
- 复用现有消息协议，新增 `INSTALL_FORK_HOOK` / `UNINSTALL_FORK_HOOK` 消息类型

**Server 与 Zygote 中 agent 的通信**：

```
Server                                  Zygote (agent in Zygote mode)
  |                                       |
  |--- INSTALL_FORK_HOOK(pkg) ----------->|  agent 安装 Java Hook
  |                                       |
  |              [target app forked]      |
  |                                       |--- Child agent activates
  |<---- AGENT_READY(child_pid) ----------|    (连回 server)
  |                                       |
  |--- UNINSTALL_FORK_HOOK ------------->|  agent 卸载 Java Hook
  |                                       |
```

### M3: 移除 ncore 依赖 (1 天)

完成 M1+M2 后：
- 移除 `ninjector_compat.cpp` 中 `PrepareSpawnInZygote()` / `ClearSpawnInZygote()`
- 移除 `server_runtime.cpp` 中 `ResolveNcorePathFromRuntimeDirectory()`
- 移除 `NinjectorSpawnInjector` 中 ncore 相关的 ops
- 移除 `ninjector_compat.h` 中 `GetDefaultNcorePath()`
- 清理构建系统中 ncore 相关引用
- 更新测试

### M4 (可选): memfd_create 注入 — 消除磁盘落盘 (2-3 天)

完成 M1-M3 后，agent 仍需落盘为文件供 Zygote dlopen。此步骤对齐 Frida 的 `Linjector.inject_library_fd()`:

```cpp
bool InjectSoByMemfd(int pid, const uint8_t* so_data, size_t so_size) {
    // 1. 在 SERVER 进程中创建 memfd
    int memfd = syscall(SYS_memfd_create, "nook-agent", MFD_CLOEXEC);
    write(memfd, so_data, so_size);

    // 2. 通过 /proc/SERVER_PID/fd/N 构造路径
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/fd/%d", getpid(), memfd);

    // 3. 在目标进程中 remote dlopen 这个路径
    bool ok = RemoteDlopen(pid, path);

    close(memfd);
    return ok;
}
```

**Android SELinux 兼容方案**：
- 方案 A: `/proc/SERVER_PID/fd/N`（server 的 memfd，目标进程通过 procfs 访问）
- 方案 B: 在目标进程中 remote 调用 `memfd_create` + `write`，再 `dlopen("/proc/self/fd/N")`
- 方案 C: 回退到临时文件 + dlopen + 删除（最兼容）

### P2 完成后部署模型

```
部署:
  adb push nook-server /data/local/tmp/nook/
  ./nook-server

运行时自动生成 (用户不需关心):
  /data/local/tmp/nook/
    ├── nook-server              # 单文件，内嵌 agent blob
    ├── libnook-agent.so         # 自动释放 (M4 后可消除)
    ├── nook.sock                # 运行时
    └── spawn_markers/           # 运行时 (P3 改为 IPC)

已消除:
    ├── libncore.so              ← 彻底消灭，不是嵌入
    ├── libc++_shared.so         ← Phase B 已消除
```

### 风险与回退

| 风险 | 影响 | 缓解措施 |
|------|------|----------|
| Java Hook 在 Zygote 中不稳定 | M1 失败 | 回退到 inline hook specializeCommon (选项 C) |
| Android 12+ USAP 路径遗漏 | 部分机型 spawn 失败 | 同时 hook nativeSpecializeAppProcess |
| Agent 在非目标子进程的内存开销 | 内存浪费 | agent 不激活时仅占 .so 映射空间，无运行时开销 |
| memfd SELinux 限制 (M4) | 无法内存注入 | 保留临时文件回退路径 |

---

## Hook 引擎架构升级

### 背景

step9.md 的分析表明，Nook 与 Frida 在 hook 引擎层有 4 个关键架构差异，直接影响**运行时性能**和**正确性**。

### 热路径对比

**Nook 当前**：
```
目标函数被调用
  → InlineHookReplacementEntry<N>()     // 固定槽位，运行时分发
    → DispatchInlineHookSlot(N)          // 查表找回调
      → 构造 HookEvent                   // 每次触发都分配
        → JsRuntime::InvokeNativeHookCallbackSync()  // 进入 QuickJS
          → onEnter JS callback
        → 调用原函数
        → JsRuntime::InvokeNativeHookCallbackSync()  // 再次进入 QuickJS
          → onLeave JS callback
```

**Frida**：
```
目标函数被调用
  → on_enter_trampoline (机器码直接生成)
    → _gum_function_context_begin_invocation()
      → TLS 取 per-thread context (无锁)
      → push invocation stack entry
      → 遍历 listener list → onEnter
    → 调用原函数
  → on_leave_trampoline (仅在有 onLeave listener 时存在)
    → _gum_function_context_end_invocation()
      → pop invocation stack entry
      → 遍历 listener list → onLeave
```

### S1: Per-thread Invocation Stack (3-4 天)

**目标**：为每个线程维护独立的 hook 调用栈，通过 TLS 存取，无锁

**解决的问题**：
- 递归 hook（A hook 内触发 B hook，B 的 onLeave 不会覆盖 A 的状态）
- 嵌套 hook 的 onEnter/onLeave 正确配对
- 多线程并发 hook 无需全局锁

**设计**：

```cpp
// 对标 Frida 的 GumInvocationContext + GumInvocationStackEntry

struct InvocationEntry {
    void* original_function;        // 被 hook 的原函数
    void* return_address;           // 原始返回地址
    uint64_t args_snapshot[8];      // x0-x7 快照 (arm64)
    uint64_t retval;                // 返回值
    JSValue on_leave_callback;      // 对应的 JS onLeave (可为空)
    uint32_t hook_id;               // hook 标识
    uint16_t depth;                 // 嵌套深度
};

struct ThreadInvocationContext {
    InvocationEntry stack[64];      // 固定大小栈，够用且避免动态分配
    uint16_t depth;                 // 当前栈深度
    uint16_t ignore_level;          // guard/suppress 支持
};

// TLS 存取，无锁
static thread_local ThreadInvocationContext* tls_ctx = nullptr;

ThreadInvocationContext* GetOrCreateThreadContext() {
    if (!tls_ctx) {
        tls_ctx = new ThreadInvocationContext();
    }
    return tls_ctx;
}
```

**关键改动**：
- 新增 `src/native_hook/inline_hook/invocation_context.h/.cpp`
- 修改 `DispatchInlineHookSlot()` → 改为先 push stack entry，再调 JS
- `InlineHookReplacementEntry<N>` 的汇编模板中保存/恢复 TLS 指针

### S2: Native InvocationContext 一等公民 (2-3 天)

**目标**：参数/返回值在 native 层管理，JS 层只做薄封装，消除每次 hook 触发的对象构造开销

**设计**：

```cpp
// native 层直接操作参数，不构造中间 HookEvent
void OnEnterNative(ThreadInvocationContext* ctx, InvocationEntry* entry) {
    // args 已经在 entry->args_snapshot 里 (trampoline 汇编保存)
    // JS bridge 只在需要时把 args[n] 包装为 NativePointer
    // 而不是预先构造完整 HookEvent 对象
}
```

**关键改动**：
- 重构 `nook_native_js_bridge.cpp` 中的回调路径
- `InvokeNativeHookCallbackSync()` 接受 `InvocationEntry*` 而不是 `HookEvent`
- JS 层的 `args[n]` 改为惰性求值（访问时才创建 NativePointer，不访问则零开销）

### S3: 按需 onLeave Trap (1-2 天)

**目标**：用户只注册 `onEnter` 时，不安装 leave trampoline，直接返回原函数

**设计**：

```cpp
struct HookRecord {
    // ... 现有字段 ...
    bool has_on_leave;    // Interceptor.attach() 时确定
};

// trampoline 生成时根据 has_on_leave 决定:
// - true:  生成完整 enter + leave trampoline
// - false: 生成 enter trampoline，leave 直接跳回原返回地址
```

**收益**：大量只用 `onEnter` 做日志/追踪的场景，性能接近翻倍。

### S4: Enter/Leave Trampoline Codegen 分离 (3-5 天)

**目标**：在机器码生成层直接拆分 enter/leave 路径，消除运行时分发的间接跳转

**设计**：

```
当前 (固定槽位 + 运行时分发):
  hook 触发 → InlineHookReplacementEntry<N> → DispatchInlineHookSlot(N) → 查表 → 回调

目标 (per-hook codegen):
  hook 触发 → on_enter_trampoline_for_hook_42 → 直接进入对应 context → 回调
              (trampoline 里硬编码了 hook_id 和 callback 指针，零查表)
```

**关键改动**：
- 修改 `arm64_writer` 生成 per-hook 的定制 trampoline
- 每个 trampoline 内联 hook_id 和 ThreadInvocationContext 操作
- 移除固定槽位限制（`InlineHookReplacementEntry<0..N>` → 动态生成）

---

## 整体路线图

| 阶段 | 任务 | 类型 | 依赖 | 预估 | 状态 |
|------|------|------|------|------|------|
| P1 | Agent 嵌入 Server | 部署 | — | — | ✅ 已完成 |
| **M1** | **Agent Zygote 模式 + fork hook** | **架构** | **P1** | **3-4 天** | 待开始 |
| **M2** | **Server spawn 流程改造** | **架构** | **M1** | **2-3 天** | 待开始 |
| **M3** | **移除 ncore 依赖** | **清理** | **M2** | **1 天** | 待开始 |
| M4 | memfd_create 注入 | 隐蔽性 | M3 | 2-3 天 | 可选 |
| S1 | Per-thread invocation stack | 引擎 | 无 | 3-4 天 | 待开始 |
| S2 | Native InvocationContext | 引擎 | S1 | 2-3 天 | 待开始 |
| S3 | 按需 onLeave trap | 引擎 | S1 | 1-2 天 | 待开始 |
| S4 | Trampoline codegen 分离 | 引擎 | S1+S2 | 3-5 天 | 待开始 |
| P3 | spawn 协调改为 IPC | 部署 | 可独立 | 2-3 天 | 待开始 |

**执行顺序**：M1→M2→M3（消除 ncore，~1 周），然后 S1→S2→S3→S4（引擎升级）。M4 和 P3 可在任意时机穿插。

**最终目标**：
- 部署：`adb push nook-server` → 单文件，零外部依赖
- 架构：与 Frida 对齐 — agent 自带 fork handler，server 自包含
- 性能：per-thread TLS context，按需 onLeave，codegen trampoline

## 2026-05-09 M1/M2 foundation progress

- Added an internal RPC dispatch foundation in src/framework/NookCommInternal.h and src/framework/NookCommInternal.cpp.
- NookComm.cpp now routes internal RPC handlers and public RPC handlers through one composed dispatch path instead of relying on a single internal handler slot.
- This removes the immediate collision between script runtime bridge RPC handling and the upcoming spawn-control RPC handling.
- Added SessionRegistry::RegisterAgentProcessName(...) and SessionRegistry::FindAgentSessionByProcessName(...) so server-side code can address long-lived agent sessions such as zygote64 by process name.
- HandleAgentReady(...) now records both pid and process name for each authoritative agent connection.
- Added focused host-side regression tests:
  - 	ests/communication/test_nook_internal_rpc_dispatch.cpp`r
  - 	ests/communication/test_session_registry.cpp`r
  - extended 	ests/communication/test_server_components.cpp`r

### Verified in this batch

- g++ -std=c++17 -I . -I include -I src tests/communication/test_nook_internal_rpc_dispatch.cpp src/framework/NookCommInternal.cpp -o build/test_nook_internal_rpc_dispatch.exe`r
- uild/test_nook_internal_rpc_dispatch.exe`r
- g++ -std=c++17 -I . -I include -I src tests/communication/test_session_registry.cpp server/session_registry.cpp src/communication/session/session.cpp src/communication/transport/transport.cpp src/communication/protocol/frame.cpp -o build/test_session_registry.exe`r
- uild/test_session_registry.exe`r
- g++ -std=c++17 -I . -I include -I src tests/communication/test_server_components.cpp server/session_registry.cpp src/communication/session/session.cpp src/communication/handler/message_dispatcher.cpp src/communication/transport/transport.cpp src/communication/protocol/frame.cpp -o build/test_server_components.exe`r
- uild/test_server_components.exe`r
- g++ -std=c++17 -I . -I include -I src tests/communication/test_server_handlers.cpp server/server_handlers.cpp server/spawn_controller.cpp server/session_registry.cpp src/communication/session/session.cpp src/communication/handler/message_dispatcher.cpp src/communication/transport/transport.cpp src/communication/transport/spawn_marker.cpp src/communication/transport/path_utils.cpp src/communication/protocol/frame.cpp src/communication/protocol/tlv.cpp src/communication/protocol/messages.cpp -o build/test_server_handlers.exe`r

### Still not done

- No zygote-side spawn-control RPC methods are registered yet.
- No server-side helper sends 
ook.spawn.installForkHook / 
ook.spawn.uninstallForkHook yet.
- Default spawn path still goes through PrepareSpawnInZygote(... ncore ...) and spawn_marker compatibility flow.
- libncore.so is therefore still part of the active spawn implementation even though the dispatch/control-plane foundation is now ready for the next step.


## 2026-05-09 M1/M2 control-path progress

- Repaired the broken `src/framework/NookComm.cpp` intermediate state and removed the accidental PowerShell literal text fragments.
- Added a split initialization path in `NookComm.cpp`:
  - `NookCommInitializeImpl(false)` keeps the existing app/child auto-init behavior.
  - `NookAgentInitializeForZygoteControl()` now uses `NookCommInitializeImpl(true)` to force an early-process control connection from zygote/zygote64 instead of taking the deferred-child path.
- Added `src/framework/nook_zygote_control.cpp` to both `NOOK_RUNTIME_SRC` and `NOOK_AGENT_SRC` in `build/android/Android.mk`.
- `server/server_main.cpp` now has server-owned zygote-control RPC helpers:
  - `CallZygoteControlRpc(...)`
  - `InstallZygoteForkHook(...)`
  - `UninstallZygoteForkHook(...)`
- `NinjectorSpawnInjector` is now wired from `server_main.cpp` with those callbacks, so spawn can try:
  1. inject agent into zygote
  2. send `nook.spawn.installForkHook`
  3. launch target and wait for callback
  4. send `nook.spawn.uninstallForkHook`
  5. fall back to legacy `ncore` prepare/clear path if the control path fails
- The current zygote-control implementation is still a bridge/skeleton based on `pthread_atfork(...)` + internal RPC registration. It is not yet the final Frida-style `nativeForkAndSpecialize` / `nativeSpecializeAppProcess` solution described earlier in this document.

### Verified in this batch

- Host-side test passed:
  - `build/test_ninjector_spawn_injector_step10_v3.exe`
- Existing server handler regression test still passed after the control-path wiring:
  - `build/test_server_handlers_step10_v2.exe`
- Android NDK build reached compilation of the updated runtime/server sources, including:
  - `nook_zygote_control.cpp`
  - `NookComm.cpp`
  - `server_main.cpp`
  - `ninjector_spawn_injector.cpp`
- The NDK build stopped at the packaging/strip stage because `libs/arm64-v8a/libnook.so` was locked (`Permission denied`). This is an output-file contention issue, not a compile error in the new step10 code.

### Still not done

- The zygote child activation path is still the temporary `pthread_atfork(...)` bridge, not the final ART specialization hook path.
- Default spawn still depends on `spawn_marker` compatibility and on legacy `ncore` fallback when the new control path fails.
- `libncore.so` is therefore not removed yet.
- Device-side validation of the new control path still needs a clean Android build artifact and on-device smoke testing.

## 2026-05-09 zygote-control rollback note

- On-device testing showed that the current experimental zygote-control path is not safe to keep enabled by default.
- The concrete failure was no longer just a spawn timeout. `zygote64` itself crashed during `nativeForkAndSpecialize(...)`, and the system log showed:
  - process: `zygote64`
  - signal: `SIGABRT`
  - abort message: `terminating with uncaught exception of type std::overflow_error: __next_prime overflow`
- This also explains the user-visible secondary symptom: after starting and stopping `nook-server`, the device could later reboot or restart critical services during app/process collection, because the experimental zygote path had already polluted the long-lived zygote process.
- Root cause summary:
  - the current bridge injects the full agent/comm/runtime control plane into `zygote64`
  - it relies on `pthread_atfork(...)` instead of hooking the ART specialization path
  - this is not equivalent to Frida's design and is too invasive for the system zygote process
- Temporary safety decision:
  - `NinjectorSpawnInjector` now keeps the zygote-control path behind an explicit opt-in env var: `NOOK_ENABLE_EXPERIMENTAL_ZYGOTE_CONTROL=1`
  - default server behavior is back to the stable legacy `ncore` spawn path
- This is a deliberate rollback of the default behavior, not an abandonment of the single-server goal.
- The final direction remains:
  - do not run the full normal agent path inside zygote
  - replace the `pthread_atfork(...)` bridge with a real `nativeForkAndSpecialize` / `nativeSpecializeAppProcess` hook path
  - only re-enable zygote-control by default after that path is validated on-device

## 2026-05-10 legacy fallback packaging progress

- Kept the default spawn path on the stable legacy `ncore` backend, but removed the need for the operator to stage `libncore.so` manually beside `nook-server`.
- Added an in-repo embedded `ncore` delivery path parallel to the existing embedded agent path:
  - new generated asset header: `server/generated/nook_embedded_ncore_blob.h`
  - new generator script: `tools/build_embedded_ncore_blob.ps1`
- `server/server_main.cpp` now resolves `NOOK_NCORE_PATH` in this order:
  1. explicit `NOOK_NCORE_PATH`
  2. sidecar `runtime_dir/libncore.so`
  3. embedded `ncore` blob materialized to `runtime_dir/libncore.so`
- `server/server_runtime.*` now exposes `ResolveNcorePathFromEnvironmentAndRuntimeDirectory(...)` so the fallback path no longer has to hardcode the device location at server startup.
- This is intentionally a deployment-surface reduction only:
  - it does not change the default backend selection
  - it does not re-enable the experimental zygote-control path
  - it does not mean `ncore` is architecturally finished

### Verified in this batch

- Host runtime test passed after adding `NOOK_NCORE_PATH` resolution coverage:
  - `build/test_server_runtime.exe`
- Host regression scan passed for the new fallback packaging constraints:
  - `build/test_ncore_fallback_regressions.exe`
- `tools/build_embedded_ncore_blob.ps1` generated:
  - `server/generated/nook_embedded_ncore_blob.h`

### Still not done

- `libncore.so` is still part of the active legacy spawn backend, only no longer a required manual deployment artifact when using the embedded path.
- The final Frida-aligned direction is still:
  - move spawn control into a real zygote specialization path
  - keep `ainject`/`ncore` only as fallback
  - eventually remove `spawn_result.json` and `spawn_markers/` from the steady-state path

## 2026-05-10 Android 11 specialize-path minimum cut

- Replaced the old `pthread_atfork(...)`-based zygote-control skeleton in `src/framework/nook_zygote_control.cpp` with a real minimum specialize-driven path for the current device target (Android 11 / SDK 30).
- The new zygote-control path now installs Java static-native hooks for:
  - `com.android.internal.os.Zygote.nativeForkAndSpecialize(...)`
  - `com.android.internal.os.Zygote.nativeSpecializeAppProcess(...)`
- The hook callbacks explicitly call the original ART entry through the existing Java-hook runtime (`CallOriginalNow(...)`) and only activate the inherited child-side agent when:
  - the fork/specialize result indicates the child path, and
  - the zygote `niceName` argument matches the requested target package.
- Added child-side comm-state rebuild in `src/framework/NookComm.cpp`:
  - clears the inherited zygote-side agent connection state after fork
  - preserves the spawn token
  - temporarily overrides process-name resolution with the target package until the child reconnects
  - rebuilds a fresh child session instead of reusing the zygote control socket
- This keeps the architecture aligned with the intended Frida-like direction:
  - zygote keeps only the control hook surface
  - the real runtime/session is re-established in the specialized child
  - legacy `ncore` remains available as fallback

### Verified in this batch

- New source regression test passed:
  - `build/test_zygote_control_regressions.exe`
- Existing embedded-`ncore` fallback regression test still passed:
  - `build/test_ncore_fallback_regressions.exe`
- Existing runtime-path regression test still passed:
  - `build/test_server_runtime.exe`
- Android NDK build passed for:
  - `nook-server`
  - `libnook-agent.so`

### Still not done

- This batch only covers the current device class:
  - Android 11 / SDK 30
- Android 12+ `ZygoteCommandBuffer.nativeForkRepeatedly(...)` is not wired yet.
- The zygote path still injects the normal agent artifact into zygote and therefore still needs careful on-device validation before becoming the default backend.
- `ncore` is still retained as the stable fallback backend until the specialize path is validated across the target matrix.

## 2026-05-10 embedded ncore lazy materialization cleanup

- Tightened the legacy fallback packaging so embedded `ncore` now behaves more like an internal Frida-style helper asset instead of a server-start sidecar.
- `server/server_main.cpp` no longer materializes embedded `libncore.so` eagerly during server startup.
  - if `NOOK_NCORE_PATH` is set, it is still honored
  - if a real sidecar `runtime_dir/libncore.so` exists, it is still used as-is
  - otherwise the embedded blob is only materialized when the legacy spawn path actually needs it
- `server/ninjector_spawn_injector.cpp` now owns the legacy fallback asset lifecycle:
  - before `prepare_spawn`, it ensures `libncore.so` exists
  - if the file had to be materialized from the embedded blob for this spawn transaction, that fact is tracked in the transaction state
  - after successful `clear_spawn`, the transaction attempts to delete only the embedded-on-demand copy
  - pre-existing sidecar copies are preserved
- `server/server_runtime.*` was extended with shared helpers for:
  - embedded shared-object path construction
  - embedded `ncore` cleanup
  - safe best-effort file removal
  - parent directory creation before materializing embedded assets

### Verified in this batch

- Host runtime regression test still passed after the runtime helper refactor:
  - `build/test_server_runtime.exe`
- Host spawn-injector regression test passed after adding lazy `ncore` coverage:
  - `build/test_ninjector_spawn_injector.exe`
- New host assertions now cover:
  - `libncore.so` path construction
  - stale embedded `ncore` cleanup
  - on-demand materialization for legacy spawn
  - preserving an operator-provided sidecar `libncore.so`

### Effect on deployment model

- User-facing deployment stays on the same target trajectory:
  - push `nook-server`
  - do not require manually pushing `libncore.so`
- Runtime behavior is now closer to Frida's packaging model:
  - helper payload stays embedded until actually needed
  - no eager server-start extraction of `libncore.so`
- This is still not the final Frida-equivalent architecture:
  - legacy spawn still needs `ncore`
  - `spawn_markers/` and callback files still exist
  - true Frida alignment still requires moving spawn coordination fully off the file path and eventually removing `ncore` from the primary backend

## 2026-05-10 packaging follow-up

- Added a standard packaging entrypoint:
  - `tools/build_single_server_package.ps1`
- The purpose is to eliminate the previous stale-blob failure mode where:
  - `libnook-agent.so` or `libncore.so` was rebuilt
  - but `nook-server` was rebuilt against an older embedded blob header
- The packaging script now enforces the correct order:
  1. build `nook_agent`
  2. build `nook_ncore`
  3. copy the fresh artifacts into `libs/arm64-v8a/`
  4. regenerate:
     - `server/generated/nook_embedded_agent_blob.h`
     - `server/generated/nook_embedded_ncore_blob.h`
  5. rebuild `nook_server`

### Recommended command

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build_single_server_package.ps1
```

### Recommended device deployment

```powershell
adb push .\libs\arm64-v8a\nook-server /data/local/tmp/nook/nook-server
```

- For the normal single-server workflow, do not manually push:
  - `libnook-agent.so`
  - `libncore.so`
- They are now packaging/runtime internals unless a debugging session explicitly needs a standalone sidecar.

## 2026-05-10 single-server runtime surface reduction follow-up

- Continued the Frida-style single-server packaging work and reduced the steady-state device surface further.
- `server/ninjector_compat.cpp` and `server/ninjector_spawn_injector.cpp` now prefer an embedded-`ncore` spawn path:
  - `nook-server` injects the embedded `ncore` blob into zygote through `memfd_create()` + `/proc/<server-pid>/fd/<n>`
  - the legacy spawn finalization path reuses the same remote `ncore` handle for `aclear()`
  - file-backed `libncore.so` is now only a fallback, not the normal path
- `src/communication/transport/unix_transport.cpp` now supports abstract Unix domain sockets:
  - default Android server/agent IPC no longer requires a filesystem socket node
  - `@name` is interpreted as an abstract socket address
  - explicit filesystem socket paths remain compatible
- `server/server_runtime.cpp` and `server/server_main.cpp` were aligned with the transport layer so server and agent derive the exact same default abstract socket name.
- Fixed direct foreground launch through `cd /data/local/tmp/nook && ./nook-server`:
  - relative executable paths are now resolved to absolute paths before deriving `runtime_dir`
  - agent paths are therefore emitted as absolute paths instead of `./libnook-agent.so`
  - this removes the zygote-child `dlopen("./libnook-agent.so")` failure mode seen during spawn
- Absolute paths are also lexically normalized so logs no longer retain redundant `/.` path segments.

### Verified in this batch

- Device deployment works with only:
  - `nook-server`
- Runtime directory no longer needs or produces:
  - `libncore.so`
  - `libnook-agent.so`
  - `nook.sock`
- Successful spawn/hook validation was observed with:
  - embedded `ncore` proc-fd injection
  - abstract Unix socket server/agent IPC
  - direct foreground `./nook-server` launch

### Current operator notes

- Foreground launch:

```sh
cd /data/local/tmp/nook
./nook-server
```

- Background launch without creating `server.out` / `server.err`:

```sh
nohup /data/local/tmp/nook/nook-server >/dev/null 2>/dev/null < /dev/null &
```

- At this point the normal single-server deployment target is:
  - push only `nook-server`
  - no manual sidecar `.so`
  - no filesystem Unix socket

## 2026-05-25 root tmp single-server runtime-dir propagation fix

- A real-device regression was reproduced when `nook-server` was launched from:
  - `/data/local/tmp/nook-server`
- The same build worked when launched from:
  - `/data/local/tmp/nook/nook-server`
- The user-visible failure on the default spawn path was:
  - `spawn agent-ready failed ... spawn authoritative agent ready timed out`

### Root cause

- This was not a stale build artifact issue.
- The parent server and spawned child were deriving different runtime directories and therefore different abstract Unix socket names.
- On the failing path:
  - server runtime dir resolved to `/data/local/tmp`
  - spawned child still preserved `/data/local/tmp/nook`
- As a result:
  - the server listened on the abstract socket hashed from `/data/local/tmp`
  - the child tried to connect to the abstract socket hashed from `/data/local/tmp/nook`
  - the authoritative child could never connect back, so host-side spawn ready timed out

### Why the mismatch happened

- The legacy spawn path previously prepared zygote state with:
  - target package
  - target agent path
  - spawn token
- But it did not explicitly seed the zygote-side `NOOK_RUNTIME_DIR` for that spawn transaction.
- That allowed zygote or child-side code to keep using a stale legacy runtime dir value from older `/data/local/tmp/nook` assumptions.

### Fix

- `server/ninjector_spawn_injector.h`
- `server/ninjector_spawn_injector.cpp`
- `server/ninjector_compat.h`
- `server/ninjector_compat.cpp`

- The legacy spawn route now threads the authoritative `runtime_dir` all the way through:
  - `PrepareSpawnInZygoteEmbedded(...)`
  - `PrepareSpawnInZygote(...)`
  - `ClearSpawnInZygoteEmbedded(...)`
  - `ClearSpawnInZygote(...)`
- During prepare:
  - zygote now receives an explicit `NOOK_RUNTIME_DIR=<authoritative runtime dir>`
- During clear/finalize:
  - the zygote-side `NOOK_RUNTIME_DIR` written for that spawn transaction is explicitly removed

### Result

- Root-tmp single-server launch now behaves the same way as the nested runtime-dir launch:
  - `/data/local/tmp/nook-server`
  - `/data/local/tmp/nook/nook-server`
- The normal default spawn path no longer depends on the old `/data/local/tmp/nook` runtime-dir residue.
- Real-device verification after rebuilding confirmed:
  - `/data/local/tmp/nook-server` startup works
  - default spawn no longer hits `spawn authoritative agent ready timed out`
  - hook installation succeeds again on the previously failing path

### Packaging / release implication

- The release `nook-server` can now be tested directly from `/data/local/tmp/` without relying on the historical `/data/local/tmp/nook/` directory layout.
- This removes one more hidden environment dependency from the single-server deployment model.
