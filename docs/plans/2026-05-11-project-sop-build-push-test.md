# Project SOP: Build, Embedded Blob Refresh, Push, And Test Handoff

## 目的

这份 SOP 用来防止后续上下文压缩后重复踩坑，约束 Nook 项目在开发过程中的固定操作顺序。

适用场景：

- 修改了 `libnook-agent.so` 相关代码
- 修改了 `libncore.so` 相关代码
- 修改了 `nook-server`
- 需要推送到真机复测

## 协作边界

- 我负责：
  - 修改代码
  - 编译
  - 刷新 embedded blob 头文件
  - 重新编译最终产物
  - 推送到设备
  - 设置设备侧权限
  - 给出你要执行的测试命令

- 你负责：
  - 在真机上运行 `nook-server` / `nook-cli`
  - 执行目标 case
  - 把现象和日志回传给我

## 核心原则

### 1. 修改 agent / ncore 后，不能只重新编一次 `nook-server`

如果改动涉及：

- `libnook-agent.so`
- `libncore.so`
- 它们依赖的 runtime / inject / spawn / hook 逻辑

那么必须注意：

- `nook-server` 里内嵌的是 `server/generated/nook_embedded_agent_blob.h`
- 以及 `server/generated/nook_embedded_ncore_blob.h`

所以正确顺序不是：

1. 改代码
2. `ndk-build`
3. 直接推 `nook-server`

而是：

1. 改代码
2. 编出新的 `libnook-agent.so` / `libncore.so`
3. 重新生成 embedded blob 头
4. 再重新编 `nook-server`
5. 再推设备

否则会出现：

- 设备上的 `nook-server` 带着旧 embedded agent
- 真机表现和本地源码不一致
- 看起来像“代码没生效”

### 1.1 embedded blob 刷新和 `nook-server` 重编必须严格串行，不能并行

这是已经踩过的真实坑。

错误做法：

1. 一边刷新 `server/generated/nook_embedded_agent_blob.h`
2. 一边同时重编 `nook-server`

这样会导致：

- `nook-server` 很可能还是基于旧 blob 头编出来
- 后续即使 blob 刷新成功，已经编好的 `nook-server` 也不会自动带上新 embedded agent
- 真机现象会表现为：
  - 本地源码和字符串都改了
  - 设备日志却仍然跑旧逻辑
  - 误以为“逻辑修复没生效”

正确做法：

1. 先编出最新 agent / ncore
2. 再刷新 embedded blob 头
3. 确认 blob 头已更新
4. 最后重新编 `nook-server`
5. 再推送设备

结论：

- 刷新 blob 和重编 `nook-server` 绝对不能并行
- 这一步必须串行执行

### 1.2 刷新 embedded agent blob 时，必须显式锁定来源为 `libs/arm64-v8a/libnook-agent.so`

这也是已经踩过的坑。

如果直接运行：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build_embedded_agent_blob.ps1
```

脚本会在多个候选产物之间自动选择。仓库里如果同时存在：

- `obj/local/arm64-v8a/libnook-agent.so`
- `build/android/obj/local/arm64-v8a/libnook-agent.so`
- `libs/arm64-v8a/libnook-agent.so`

它可能会选中一个不是最终安装产物的候选，导致：

- embedded blob 大小异常
- 设备上的 embedded agent 与 `libs` 目录中的最终 agent 不一致
- server 和 embedded agent 的版本错位

正确做法：

```powershell
$env:NOOK_EMBEDDED_AGENT_SOURCE = (Resolve-Path .\libs\arm64-v8a\libnook-agent.so).Path
powershell -ExecutionPolicy Bypass -File .\tools\build_embedded_agent_blob.ps1
```

不要依赖脚本自动挑选候选文件。

### 2. 优先使用标准 ndk-build 产物

默认使用：

- `libs/arm64-v8a/nook-server`
- `libs/arm64-v8a/libnook-agent.so`
- `libs/arm64-v8a/libncore.so`

不要把临时手工链接产物当作正式运行时产物，除非明确就是在验证临时诊断版本。

### 3. 设备推送后要统一设权限

推送完成后至少保证：

- `nook-server` 可执行
- 运行目录权限足够

否则会出现：

- server 起不来
- 启动方式不同导致路径判断不同
- 真机表现不稳定

## 标准构建顺序

### A. 只改 `nook-server`，未改 agent / ncore embedded 内容

可直接：

1. 编译 `nook_server`
2. 推 `nook-server`

### B. 改了 agent 相关逻辑

必须执行：

1. 编译 `nook_agent`
2. 显式指定 `NOOK_EMBEDDED_AGENT_SOURCE=libs/arm64-v8a/libnook-agent.so`
3. 运行：
   - `tools/build_embedded_agent_blob.ps1`
4. 确认 `server/generated/nook_embedded_agent_blob.h` 已更新
5. 再编译 `nook_server`
6. 推送新 `nook-server`

### C. 改了 ncore 相关逻辑

必须执行：

1. 编译 `nook_ncore`
2. 运行：
   - `tools/build_embedded_ncore_blob.ps1`
3. 再编译 `nook_server`
4. 推送新 `nook-server`

### D. 同时改了 agent / ncore / server

必须执行完整顺序：

1. 先完整 `ndk-build`
2. 显式指定 embedded agent 来源为 `libs/arm64-v8a/libnook-agent.so`
3. 刷新：
   - `tools/build_embedded_agent_blob.ps1`
   - `tools/build_embedded_ncore_blob.ps1`
4. 再次编译 `nook_server`
5. 推送最终产物

## 推荐命令

### 1. 完整 Android 构建

```powershell
E:\SDK\ndk\25.2.9519653\ndk-build.cmd -B NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=build/android/Android.mk NDK_APPLICATION_MK=build/android/Application_static.mk -j4
```

### 2. 刷新 embedded agent blob

```powershell
$env:NOOK_EMBEDDED_AGENT_SOURCE = (Resolve-Path .\libs\arm64-v8a\libnook-agent.so).Path
powershell -ExecutionPolicy Bypass -File .\tools\build_embedded_agent_blob.ps1
```

### 3. 刷新 embedded ncore blob

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build_embedded_ncore_blob.ps1
```

### 4. 只重编 `nook_server`

```powershell
E:\SDK\ndk\25.2.9519653\ndk-build.cmd -B NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=build/android/Android.mk NDK_APPLICATION_MK=build/android/Application_static.mk APP_ABI=arm64-v8a APP_MODULES=nook_server -j4
```

## 标准设备推送流程

默认设备运行目录：

- `/data/local/tmp/nook`

### 清理并创建目录

```powershell
adb shell "su -c 'rm -rf /data/local/tmp/nook && mkdir -p /data/local/tmp/nook && chmod 777 /data/local/tmp/nook'"
```

### 推送最小运行产物

当前如果目标是单 server 路线，优先只推：

```powershell
adb push .\libs\arm64-v8a\nook-server /data/local/tmp/nook/nook-server
```

如果当前验证分支明确还需要 sidecar，也由我决定是否额外推：

- `libnook-agent.so`
- `libncore.so`
- `libnook.so`
- `libc++_shared.so`

### 设置权限

```powershell
adb shell "su -c 'chmod 755 /data/local/tmp/nook/nook-server; chmod 644 /data/local/tmp/nook/* 2>/dev/null || true; chmod 755 /data/local/tmp/nook/nook-server'"
```

如果推送了 `.so`，通常保持可读即可；server 必须可执行。

## 启动与测试交接

### 启动 server

通常由你执行：

```powershell
adb shell su -c '/system/bin/linker64 /data/local/tmp/nook-server --enable-zygote-control >/data/local/tmp/nook-server.out 2>/data/local/tmp/nook-server.err </dev/null & echo $! >/data/local/tmp/nook-server.pid'
```

### 停止旧 server

不要再用会误伤命令链本身的 `pkill -f`。

改用 pid 文件：

```powershell
adb shell su -c 'if [ -f /data/local/tmp/nook-server.pid ]; then kill $(cat /data/local/tmp/nook-server.pid) 2>/dev/null; rm -f /data/local/tmp/nook-server.pid; fi'
```

如果需要在推送前清理历史输出和诊断文件：

```powershell
adb shell su -c 'if [ -f /data/local/tmp/nook-server.pid ]; then kill $(cat /data/local/tmp/nook-server.pid) 2>/dev/null; rm -f /data/local/tmp/nook-server.pid; fi; rm -f /data/local/tmp/nook-server.out /data/local/tmp/nook-server.err /data/local/tmp/nook-fd-snapshot-*'
```

### 推送后必须校验设备侧是否真的是最新文件

至少检查：

- `sha256sum`
- `stat/ls -l` 的文件大小

例如：

```powershell
adb shell su -c 'sha256sum /data/local/tmp/nook-server; stat -c "%s" /data/local/tmp/nook-server'
```

只有当设备侧 hash/size 和本地 `libs/arm64-v8a/nook-server` 一致时，才能开始真机复测。

```powershell
adb shell "su -c 'nohup /system/bin/linker64 /data/local/tmp/nook/nook-server >/data/local/tmp/nook/server.out 2>/data/local/tmp/nook/server.err < /dev/null &'"
```

### 查看设备侧状态

```powershell
adb shell "su -c 'sleep 1; cat /data/local/tmp/nook/server.err; echo ----; cat /data/local/tmp/nook/server.out; echo ----; ls -l /data/local/tmp/nook'"
```

### 真机测试分工

推送完成后：

- 我给出你要执行的 `nook-cli` 命令
- 你负责在真机上跑
- 你把现象、CLI 输出、设备日志给我

## 调试时必须检查的事项

如果出现“改了代码但现象没变”，优先检查：

1. `libnook-agent.so` / `libncore.so` 改完后是否刷新了 embedded blob 头
2. 如果改了 `server/symbi/stub_src/stub.c` / `stub.h` / `helper.lds`，是否先执行了 `tools/build_symbi_stub_header.ps1` 重生 `generated_stub.h`
3. 刷新 blob 后是否重新编译了 `nook-server`
4. 推送到设备的是不是最新 `libs/arm64-v8a/nook-server`
5. 设备上是否残留旧 sidecar 文件影响路径选择
6. 当前 server 是否是新启动的，不是旧进程残留
7. `tools/build_embedded_agent_blob.ps1` 是否显式锁定了 `NOOK_EMBEDDED_AGENT_SOURCE=libs/arm64-v8a/libnook-agent.so`
8. 是否错误地把“刷新 embedded blob”和“重编 `nook-server`”并行执行了
9. 设备上的 `nook-server` 文件大小是否与本地 `libs/arm64-v8a/nook-server` 一致
10. `adb push` 成功后，是否又被旧进程、旧文件或并行操作覆盖回旧包；必须用设备侧 `sha256sum` / `stat` 再确认一次
11. 如果 `libncore.so` 或 `nook-server` 体积突然膨胀到明显异常，先检查 `nook_ncore` 是否错误编入了 full embedded blob defs，避免递归嵌入

## Symbi Stub 额外约定

如果改动涉及：

- `server/symbi/stub_src/stub.c`
- `server/symbi/stub_src/stub.h`
- `server/symbi/stub_src/helper.lds`

必须先执行：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build_symbi_stub_header.ps1
```

然后再继续编译 `nook-server`。

原因：

- `server/symbi/stub_src/generated_stub.h` 是编译期产物
- `nook-server` 实际带进去的是这个生成头里的 payload 字节
- 只改 `stub.c` / `stub.h` 而不重生 `generated_stub.h`，设备上的 `symbi` 实际还是旧 stub

## 当前默认执行约定

后续没有特别说明时，默认遵循：

1. 我先改代码
2. 我按本 SOP 编译并刷新 embedded blob
3. 我推送设备并设权限
4. 我给你测试命令
5. 你跑真机测试并回传输出

## 2026-05-14 additional handoff rules

### A. Always stop old server before push

每次 push 新 `nook-server` 前，固定先做这三步：

1. 停掉旧 server
2. 清掉旧 `server.out` / `server.err`
3. 再 push 新 binary

如果不先停旧 server，就算 push 成功，`27042` 也可能还是旧进程在监听，后面的 `nook-cli` 结果全部无效。

### B. Do not trust pid files alone

`su 0 -c '/system/bin/linker64 ... & echo $! >/data/local/tmp/nook-server.pid'` 这种写 pid 文件的方式不可靠。实测 pid 文件里可能记录的是 `su` 包装进程，而不是最终的 `linker64 ... nook-server`。

停旧 server 时，必须再次用 `/proc/<pid>/cmdline` 或 `ps -A -o PID,PPID,NAME,ARGS` 复核。

### C. Verify pushed file with separate commands

push 完新 `nook-server` 后，至少分别检查：

1. `sha256sum /data/local/tmp/nook/nook-server`
2. `wc -c /data/local/tmp/nook/nook-server`
3. `ls -l /data/local/tmp/nook/nook-server`

不要把这些校验混在一条复杂的 `adb shell su 0 -c '...'` 命令里直接相信输出。三项都对上，才允许启动 server 和开始 case。

### D. A repro is invalid if port 27042 is still owned by an old server

如果新起的 server 日志里出现：

```text
host listener start failed port=27042
```

说明旧 `nook-server` 还在占端口，这一轮复现无效。必须先停旧进程，再重新启动新 server，直到日志出现：

```text
server started tcp=27042
```

### E. Embedded agent refresh must be serialized

刷新 `server/generated/nook_embedded_agent_blob.h` 和重编 `nook-server` 绝对不能并行。顺序固定为：

1. 编译 `libnook-agent.so`
2. 刷新 embedded agent blob
3. 确认 blob 已更新
4. 再编译 `nook-server`

否则很容易出现“源码已经改了，但设备仍然跑旧 embedded agent”的假象。

## 2026-05-14 push / handoff supplement

### 1. push 后必须核对设备文件是否为最新

不能只看 push 成功或日志在跑，每次都要直接核对本地与设备上的 `nook-server` MD5 / size。

```powershell
Get-FileHash .\libs\arm64-v8a\nook-server -Algorithm MD5
adb shell "su -c 'md5sum /data/local/tmp/nook/nook-server /data/local/tmp/nook-server 2>/dev/null; ls -l /data/local/tmp/nook/nook-server /data/local/tmp/nook-server 2>/dev/null'"
```

如果当前还兼容旧路径，同时 push 这两个位置，避免“同名不同路径”的旧文件混淆：

```powershell
adb push .\libs\arm64-v8a\nook-server /data/local/tmp/nook/nook-server
adb push .\libs\arm64-v8a\nook-server /data/local/tmp/nook-server
```

### 2. 交接前必须停掉旧 server

不能假设“新 binary 已经 push，所以当前进程一定是新的”。在给真机测试命令前，必须先查当前 server 进程、核对 `/proc/<pid>/cmdline`、再停掉它。

```powershell
adb shell "su -c 'ps -A | grep nook-server; ps -A | grep linker64'"
adb shell "su -c 'for pid in $(pidof nook-server 2>/dev/null); do echo ==== $pid ====; tr \"\\0\" \" \" </proc/$pid/cmdline; echo; done'"
adb shell "su -c 'for pid in $(pidof nook-server 2>/dev/null); do kill -9 $pid; done'"
```

如果是 `/system/bin/linker64 /data/local/tmp/.../nook-server` 这种启动方式，`pidof nook-server` 可能找不到。此时必须配合 `ps -A | grep linker64` 和 `/proc/<pid>/cmdline` 一起看，否则很容易把旧路径上的进程当成新 server。

另外，`su 0 -c '/system/bin/linker64 ... & echo $! >/data/local/tmp/nook-server.pid'` 这种写 pid 文件的方式不可靠。实测 pid 文件里可能记录的是 `su` 包装进程，而不是最终的 `linker64 ... nook-server`。所以停旧 server 不能只信 pid 文件，必须再次用 `/proc/<pid>/cmdline` 或 `ps -A -o PID,PPID,NAME,ARGS | grep nook-server` 复核。
### 3. 每轮复现前先停旧 server，再起新 server

如果新起的 server 日志里出现：

```text
host listener start failed port=27042
```

说明旧的 `nook-server` 还在占用端口，这一轮复现无效。
此时即使你重新 push 了新 binary，`nook-cli` 也仍然会连到旧 server，看到的现象不能用于判断新改动是否生效。

最低要求：

1. 先确认旧 server 已停
2. 再起新 server
3. 再看新日志里是否出现：
   - `server started tcp=27042`
   - `unix=@nook-...`

只有确认新的 server 真正监听了 `27042`，这一轮 spawn 复现结果才可信。

不要直接在旧进程上反复跑 case。每轮复现前都先停旧 server、清理旧输出，再启动新 server：
```powershell
adb shell "su -c 'rm -f /data/local/tmp/nook-server.out /data/local/tmp/nook-server.err /data/local/tmp/nook/server.out /data/local/tmp/nook/server.err'"
adb shell "su -c 'for pid in $(pidof nook-server 2>/dev/null); do kill -9 $pid; done'"
adb shell "su -c 'ps -A | grep nook-server; ps -A | grep linker64'"
adb shell "su -c '/system/bin/linker64 /data/local/tmp/nook-server --enable-zygote-control >/data/local/tmp/nook-server.out 2>/data/local/tmp/nook-server.err </dev/null &'"
```

### 4. 设备端校验命令要拆开执行

不要把 `sha256sum`、`stat`、`ls` 之类的校验混在一条复杂的 `adb shell su 0 -c '...'` 命令里再直接相信输出。实测在这种场景下，返回内容可能和真实设备文件状态不一致，容易把一次无效 push 误判成成功。

最稳妥的做法是拆开执行，并至少做两项独立校验：

```powershell
Get-FileHash .\libs\arm64-v8a\nook-server -Algorithm SHA256
adb shell su 0 -c 'sha256sum /data/local/tmp/nook-server'
adb shell su 0 -c 'wc -c /data/local/tmp/nook-server'
adb shell su 0 -c 'ls -l /data/local/tmp/nook-server'
```

只有当设备端 hash、字节数、`ls -l` 都和本地 `libs/arm64-v8a/nook-server` 一致时，这一轮真机复现才有效。

### 5. 使用 `/data/local/tmp/nook/nook-server` 路径时，启动前也要先停旧 server

如果当前实际使用的是：

```powershell
adb shell "su 0 -c '/system/bin/linker64 /data/local/tmp/nook/nook-server --enable-zygote-control >/data/local/tmp/nook/nook-server.out 2>/data/local/tmp/nook/nook-server.err </dev/null &'"
```

则每次启动前固定执行：

```powershell
adb shell "su 0 -c 'pkill -9 -f nook-server 2>/dev/null || true'"
adb shell "sleep 1; ps -A -o PID,PPID,NAME,ARGS | toybox grep nook-server"
```

push 后也要按这个实际路径拆开核对：

```powershell
Get-FileHash .\libs\arm64-v8a\nook-server -Algorithm SHA256
adb shell "su 0 -c 'sha256sum /data/local/tmp/nook/nook-server'"
adb shell "su 0 -c 'wc -c /data/local/tmp/nook/nook-server'"
adb shell "su 0 -c 'ls -l /data/local/tmp/nook/nook-server'"
```

不能只检查旧路径 `/data/local/tmp/nook-server`。如果运行路径和校验路径不是同一个文件，这一轮日志结论无效。
## 2026-05-14 zygote-control crash note

- On this Xiaomi Android 11 device with `zygisk` present, enabling
  `NOOK_ENABLE_ZYGOTE_JAVA_NATIVE_HOOKS=1` in zygote-control is not safe.
- Verified failure mode:
  `zygote64` crashes during target app start, and tombstone shows either
  `libzygisk.so -> GetStringUTFChars()` SIGSEGV or
  `JNI FatalError called: (zygote) Unable to get socket name`.
- 2026-05-14 follow-up correction:
  disabling both Java zygote hook envs is still not sufficient to make the
  current device stable. The latest repro still crashes `zygote64`, so do not
  treat `NOOK_ENABLE_ZYGOTE_JAVA_NATIVE_HOOKS=0` and
  `NOOK_ENABLE_ZYGOTE_JAVA_WRAPPER_HOOKS=0` as a proven-good configuration on
  this device.
- Latest confirmed crash signature:
  `ActivityManager: Failure starting process com.ad2001.frida0x1`
  `Caused by: android.os.ZygoteStartFailedEx: java.io.EOFException`
  together with a fresh tombstone for `zygote64`.
- Latest confirmed tombstone signature:
  `zygote64` `SIGSEGV`
  `libart.so -> art::JNI<true>::GetStringUTFChars`
  `/data/adb/modules/zygisksu/lib64/libzygisk.so`
- Therefore, if spawn timeout comes back in future, first check whether the
  timeout is secondary to a zygote crash:
  1. `ActivityManager` / `ZygoteProcess` logs for `EOFException`
  2. fresh `/data/tombstones/tombstone_*`
  3. whether `zygote64` pid changed after the repro
  Only if those are clean should the run be treated as a pure
  child-activation regression.
- Before starting a new zygote-control repro on this device, verify the manual
  baseline once after reboot:
  `adb shell am force-stop com.ad2001.frida0x1`
  `adb shell monkey -p com.ad2001.frida0x1 -c android.intent.category.LAUNCHER 1`
  If the app cannot launch cleanly without Nook, the zygote-control repro is
  invalid.
- If zygote injection fails with
  `remote_init_failed:atomic:status=4294967294`, treat it as
  `NOOK_STATUS_INVALID_ARGUMENT (-2)` from
  `NookAgentInitializeForZygoteControl()`, not as a generic timeout.
- First check whether the injected process name window is
  `pre-initialized` / `<pre-initialized>` in `NookCommApi` logs. That
  name must be treated as an early-spawn / zygote context during
  zygote-control init.

## 2026-05-14 zygote-control child-activation note

- Latest confirmed device behavior on this Xiaomi Android 11 / MIUI path:
  `zygote64` can report control-ready and can install Java native zygote
  hooks successfully, but `nativeForkAndSpecialize` /
  `nativeSpecializeAppProcess` callbacks may still never fire.
- Current evidence does not support an active `usap64/usap32` handoff on
  this device during the failing repro. The spawned app child was observed
  directly under `zygote64`.
- Treat `authoritative agent ready timeout` as a child-activation failure
  first, not as a stale-session problem, when logs already show:
  `zygote-control stage=ready-wait event=ready`
  `nook.spawn.installForkHook` success
  app `START ... cmp=com.ad2001.frida0x1/.MainActivity`
  but no child runtime `AGENT_READY stage=2`.
- Prefer checking these child-activation breadcrumbs in order:
  `setArgV0 observe current=...`
  `setArgV0 child matched nice=...`
  `spawn gate bootstrap hooks installed ...`
  `runtime bridge ensure ...`
  `AGENT_READY sent pid=... stage=2`
- `setArgV0` monitoring must stay safe for zygote use:
  do not decode the incoming `jstring` in zygote;
  only call the original function first, then read `/proc/self/cmdline`
  and match the resulting process name.
- If `setArgV0 child matched ...` appears but there is still no runtime
  `AGENT_READY`, continue along the inherited child bootstrap path
  (`ResetInheritedConnectionStateForChild` / spawn-gate bootstrap hooks /
  runtime bridge), not back to zygote-session cleanup.

## 2026-05-14 embedded-agent verification note

- When a change touches zygote-control agent code
  (`src/framework/nook_zygote_control.cpp`, `src/framework/NookComm.cpp`,
  or other `libnook-agent.so` runtime paths), `nook-server` rebuild alone
  is not sufficient.
- Required serialized order:
  1. rebuild `nook_agent`
  2. refresh `server/generated/nook_embedded_agent_blob.h` from the intended
     `obj/local/arm64-v8a/libnook-agent.so` or another explicitly chosen source
  3. rebuild `nook_server`
  4. push `/data/local/tmp/nook/nook-server`
- After push, verify both layers:
  - device file hash/size:
    `sha256sum /data/local/tmp/nook/nook-server`
    `wc -c /data/local/tmp/nook/nook-server`
  - runtime embedded-agent fingerprint from startup log:
    `embedded agent blob size=... source_size=... sha256=... source=...`
- If device-side `nook-server` hash is new but startup log still prints the
  old embedded-agent `sha256/source_size`, treat that run as invalid:
  the server binary and the embedded agent payload are still out of sync, and
  any zygote-control repro based on that run is not actionable.
- If strict zygote-control fails with
  `inject zygote agent failed: dlsym_failed:NookAgentInitializeForZygoteControl`,
  first treat the repro as a stale-device-artifact problem, not an immediate
  source-code regression:
  - verify the exact `nook-server` hash running on device
  - verify startup log `embedded agent blob size/source_size/sha256`
  - rebuild and repush the single-server package to a fresh device filename
  - restart the server from that fresh filename before analyzing code
- If `nook-server` startup fails with
  `host listener start failed port=27042`, do not trust the run. Clean stale
  server instances / port users first, then restart the server and wait for the
  normal startup line:
  `server started tcp=27042 unix=@... agent=__embedded_agent__ runtime=...`

## 2026-05-16 device recovery baseline

- If manual app launch starts hanging globally, stop spawn debugging first.
- Treat that as a dirty-device / dirty-zygote state until proven otherwise.
- Recovery order:
  1. confirm there is no live `nook-server`
  2. clean `/data/local/tmp/nook`, `/data/local/tmp/nook-server.verify-*`,
     and `/data/local/tmp/nook-fd-snapshot-*`
  3. if behavior persists, `adb reboot`
  4. wait for `adb devices` and `getprop sys.boot_completed=1`
  5. verify manual baseline with `am start -W -n <pkg>/<activity>`
- Do not continue `strict-zygote-control` repros on a device where manual app
  launch is already abnormal.
- On this device, reboot is an acceptable part of the debugging loop when
  zygote/helper residue is suspected.

## 2026-05-16 strict zygote-control startup verification

- Prefer the host-side helper instead of hand-written `adb shell su 0 -c ...`
  launch strings:
  `powershell -ExecutionPolicy Bypass -File .\tools\device_start_nook_server.ps1 -Serial <adb-serial>`
- That helper intentionally pushes to a fresh device filename on each run, then
  verifies:
  - local packaged `nook-server` sha256 / size
  - remote pushed file sha256 / size
  - startup log fingerprints for embedded:
    - `agent`
    - `zygote-helper`
    - `ncore`
- Treat these startup lines as mandatory before analyzing a strict
  `zygote-control` regression:
  - `embedded agent blob size=... source_size=... sha256=...`
  - `embedded zygote helper blob size=... source_size=... sha256=...`
  - `embedded ncore blob size=... source_size=... sha256=...`
  - `server started tcp=27042 unix=@... agent=__embedded_agent__ runtime=...`
- If the helper reports a new local/remote `nook-server` hash but startup logs
  still show an old embedded helper fingerprint, the run is invalid. Treat it
  as a stale running server / stale binary selection problem first.
- Do not use `pkill -f nook-server` as the default cleanup path in host scripts.
  On this device/shell chain it can terminate the current root shell and make
  `adb` report `Terminated`, which obscures the real server state. Prefer
  enumerating exact server pids from `ps` and killing only those.
