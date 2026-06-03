# 2026-05-10 Embedded Agent Memfd Injection

## 背景

单服务器方向已经消除了稳定路径对 `libncore.so` 的依赖，但运行目录中仍然需要预先放置 `libnook-agent.so`。
这和 Frida 的做法仍有差距。Frida 会把 agent 嵌入 server，并优先通过内存文件描述符注入，避免常规落盘路径成为主路径。

## 本次修改

1. `NinjectorSpawnInjector::InjectAgent()` 已具备“优先 embedded agent，失败再回退文件路径”的高层策略。
2. `server/ninjector_compat.cpp` 新增了 embedded agent 低层实现：
   - 从 `server/generated/nook_embedded_agent_blob.h` 读取内嵌 agent blob
   - 在 server 进程中通过 `memfd_create()` 创建匿名 fd
   - 将 agent blob 写入 memfd
   - 使用 `/proc/<server-pid>/fd/<fd>` 作为远程 `dlopen()` 路径
   - 复用现有 ptrace + remote `dlopen()` + remote `dlsym()` + init symbol 调用链
3. 在 remote init 之前，向目标进程显式注入 `NOOK_RUNTIME_DIR`，避免 memfd 路径下 agent 无法从自身路径推导运行目录。
4. 抽取了 `InvokeRemoteInitSymbolByHandle(...)`，减少文件路径注入与 memfd 注入之间的重复逻辑。

## 同时修复

`src/framework/nook_agent_runtime.cpp` 现在会把 `/proc/.../fd/...` 识别为“非稳定 agent 路径”，此时不会把它错误地当成运行目录。

否则 agent 从 memfd 载入后，会把：

`/proc/<pid>/fd/<n>`

错误解析成 runtime dir，导致后续资源定位异常。

## 预期结果

设备运行目录在 server 启动前可以只保留：

- `nook-server`

运行后至少不应再要求预置 `libnook-agent.so` 才能完成 attach/spawn。

如果目标设备或 SELinux 策略阻止 `/proc/<server-pid>/fd/<fd>` 被目标进程 `dlopen()`，当前实现会自动回退到原有文件路径注入。

## 涉及文件

- `server/ninjector_compat.cpp`
- `server/ninjector_spawn_injector.cpp`
- `src/framework/nook_agent_runtime.cpp`
