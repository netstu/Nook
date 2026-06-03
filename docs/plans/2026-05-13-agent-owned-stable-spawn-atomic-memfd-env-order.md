# 2026-05-13 agent-owned stable spawn atomic memfd env order

## 背景

在 host 侧把 `zygote-control` 的 ready/session 语义收紧后，下一步回到设备侧注入链路。

对比 attach 已稳定的路径和 zygote-control 使用的 memfd 注入路径后，发现一个关键不一致：

- `InjectEmbeddedSoByPidAtomic()` 之前是在 `dlopen()` 成功之后，才设置 `NOOK_RUNTIME_DIR`
- 但 agent 的 constructor 可能在 `dlopen()` 过程中就已经执行
- 对 zygote-control 路径，这意味着：
  - constructor 里的 auto-init 可能看不到正确的 `NOOK_RUNTIME_DIR`
  - 也可能在 host 还没准备好前就执行到不该自动执行的初始化逻辑

而 sidecar attach 路径已经有明确保护：

- 先设置 `NOOK_SKIP_AUTO_INIT=1`
- 再 `dlopen`
- 最后显式调用 `NookAgentInitialize` / `NookAgentInitializeForZygoteControl`

atomic memfd 路径之前没有保持这个一致性。

## 改动

文件：

- `server/ninjector_compat.cpp`

改动点：

1. `InjectEmbeddedSoByPidAtomic()` 在 `dlopen()` 之前就设置 `NOOK_RUNTIME_DIR`
   - 避免 constructor 早于 env 准备执行

2. 当存在 `init_symbol` 时，`dlopen()` 前额外设置：
   - `NOOK_SKIP_AUTO_INIT=1`

3. `dlopen()` 成功后仍然走显式 init：
   - `dlsym(init_symbol)` + remote call

4. 显式 init 成功后主动清理：
   - `RemoteUnsetEnv("NOOK_SKIP_AUTO_INIT")`

5. cleanup/fail 路径也做 best-effort 清理

## 结果

这让 atomic memfd 注入路径与 sidecar attach 路径在初始化语义上重新一致：

- 先准备环境
- 禁止 constructor 提前做 auto-init
- 再显式执行目标 init symbol

对于 zygote-control，这一步重点解决的是：

- memfd agent constructor 与 `NookAgentInitializeForZygoteControl()` 之间的环境顺序不一致
- 由此引发的早期连接失败、路径解析失败或不稳定 auto-init

## 验证

至少完成了本地编译与 spawn injector 回归：

```powershell
g++ -std=c++17 -I . -I include -I src tests/communication/test_ninjector_spawn_injector.cpp server/ninjector_spawn_injector.cpp server/server_runtime.cpp server/ninjector_compat.cpp src/communication/protocol/messages.cpp src/communication/protocol/tlv.cpp -o build/test_ninjector_spawn_injector_after_atomic_env.exe
& 'E:\Learn\my_program\all_my_hook\kanxue\Nook\build\test_ninjector_spawn_injector_after_atomic_env.exe'
```

## 下一步

这一改动已经足够进入真机验证，重点观察：

1. `zygote-control` 是否还会出现 `zygote control rpc timeout`
2. 是否还会出现 `remote_dlopen_failed`
3. 是否仍会出现 server 重启后同一 zygote 无法重新建立 control-ready session
