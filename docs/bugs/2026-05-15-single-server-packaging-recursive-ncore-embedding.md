# Single-Server Packaging Regression: Recursive `ncore` Embedding

## 日期

- 2026-05-15

## 现象

单文件 `nook-server` 打包链在一次重构后出现异常：

- `build/single-server-package/arm64-v8a/nook-server`
  膨胀到 `66MB+`
- 设备侧可能出现：
  - `invalid shdr offset/size`
  - `server` 无法正常启动
  - 或者虽然能启动，但运行时行为和预期源码不一致

同时，`server/generated/nook_embedded_ncore_blob.h` 异常膨胀到数百 MB。

## 根因

`build/android/Android.mk` 中的 `nook_ncore` 模块错误包含了：

- [`server/embedded_blob_defs.cpp`](E:\Learn\my_program\all_my_hook\kanxue\Nook\server\embedded_blob_defs.cpp)

而该文件会同时定义：

- `NOOK_DEFINE_EMBEDDED_AGENT_BLOB`
- `NOOK_DEFINE_EMBEDDED_NCORE_BLOB`

这导致 `libncore.so` 在构建时把：

- embedded `agent blob`
- embedded `ncore blob`

都编进了自己。

后果是：

1. `libncore.so` 递归嵌入 `libncore.so`
2. `tools/build_embedded_ncore_blob.ps1` 从 deployable `libncore.so` 生成 blob 时把这个递归产物再次固化
3. `nook-server` 再把这个递归膨胀的 `ncore blob` 嵌进去
4. 最终单文件 server 尺寸和运行时都失真

## 直接证据

构建日志曾出现：

- `nook_ncore <= embedded_blob_defs.cpp`

这是错误信号。`nook_ncore` 不应编译 full embedded blob defs。

异常尺寸表现：

- 错误时 `libncore.so` 可达到 `66MB+`
- 错误时 `nook-server` 可达到 `66MB+`
- 正常时 deployable `libncore.so` 应约为 `3.5MB`
- 修复后 `nook-server` 回到约 `7MB`

## 修复

新增 agent-only blob 定义单元：

- [`server/embedded_agent_blob_defs.cpp`](E:\Learn\my_program\all_my_hook\kanxue\Nook\server\embedded_agent_blob_defs.cpp)

其职责仅为：

- 定义 `NOOK_DEFINE_EMBEDDED_AGENT_BLOB`
- 包含 `generated/nook_embedded_agent_blob.h`

并将 `nook_ncore` 模块从：

- `server/embedded_blob_defs.cpp`

改为：

- `server/embedded_agent_blob_defs.cpp`

这样 `libncore.so` 只会内嵌 agent，不会再把 `ncore blob` 自己也编进去。

## 同时修复的配套问题

`tools/build_embedded_ncore_blob.ps1` 也同步做了两点增强：

1. 优先选择 deployable / staged 产物，而不是原始 `obj/local`
2. 在生成头文件时记录：
   - source path
   - source sha256
   - source file size

这样后续可以更快分辨“脚本选错源”还是“源产物本身就已经坏了”。

## 预防规则

后续如果再改 single-server 打包链，必须检查：

1. `nook_ncore` 是否只依赖 agent-only blob defs，而不是 full blob defs
2. `libncore.so` 的尺寸是否仍在正常范围
3. `server/generated/nook_embedded_ncore_blob.h` 尾部 metadata 是否指向 staged deployable `libncore.so`
4. `nook-server` 尺寸是否仍在预期范围，而不是突然膨胀到数十 MB

## 结论

这不是单纯的“脚本选错产物”，而是更前面的构建图错误：

- `ncore` 模块引用了错误的 blob 定义单元

如果不先修这个，任何后续 `zygote-control` / `spawn` 实机结论都不可信。
