# 2026-05-11 Spawn Backend Status And Ninjector Boundary

## 目标

当前主目标仍然是把 Nook 收敛到接近 Frida 的运行形态：

- 设备侧默认只需要一个可见的 `nook-server`
- `attach` 和 `spawn` 都由 Nook 自己的主链路稳定完成
- 不再要求用户手工推送 `libnook-agent.so` / `libncore.so`
- 后续再逐步减少运行时落盘和历史兼容层

## 当前状态

### attach

`attach` 当前已经有一条相对稳定的主路径：

- server 侧使用 Nook 自己的远程注入实现
- agent 可以由 server 内嵌 blob 提供
- 通过显式 `NookAgentInitialize` 完成初始化，而不是完全依赖构造阶段自动初始化

这条路径当前不依赖 `ncore`。

### spawn

`spawn` 当前不是单一路径，而是三套后端并存：

1. `zygote-control`
2. `symbi`
3. `legacy ncore`

其中：

- `zygote-control` 是最接近 Frida 方向的方案，但当前在真机上仍不稳定
- `symbi` 仍是实验路径，失败时存在污染 zygote 甚至触发设备重启的风险
- `legacy ncore` 仍然是当前最现实的稳定保底路径

因此，当前真实结论不是“已经彻底 Frida 化”，而是：

- 用户视角可以逐步接近“只推一个 server”
- 但 `spawn` 的稳定后端在架构上仍未完全摆脱 `ncore`

## 这轮问题的关键结论

最近这轮问题里，`zygote-control` 的 embedded memfd 路径虽然已经做了“原子化 memfd + dlopen”收敛，但本质上仍然是在目标进程里调用：

`dlopen("/proc/self/fd/N")`

这和 Frida 的最终稳定做法还不是一回事。

当前直接暴露出来的问题是：

- zygote 侧会出现 `dlopen failed: library "/proc/self/fd/xx" not found`
- 说明当前实现还不是真正稳妥的 fd-aware 远程装载
- 这条路径现在不能作为默认稳定主链

同时，`symbi` 当前虽然已增加恢复兜底，但仍然可能出现：

- `restore_original_slot_failed`
- zygote 被污染
- 设备重启

所以 `symbi` 也不适合作为默认稳定保底。

## 当前 Nook 和外部 Ninjector 的关系

### 已经内收进 Nook 的部分

当前主 server 构建并不是“直接调用外部 Ninjector 可执行文件”。

Nook 自己已经有本地注入实现，核心代码位于：

- [server/ninjector_compat.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_compat.cpp)
- [server/ninjector_spawn_injector.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ninjector_spawn_injector.cpp)
- [server/symbi_injector_local.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/symbi_injector_local.cpp)

Android 构建里，`nook-server` 直接编入的是 Nook 仓库下这些源文件，而不是外部 `Ninjector` 的 `.cpp` 主实现文件。

### 仍然耦合外部 Ninjector 的部分

但现在还不能说“已经和 Ninjector 完全解耦”。当前还残留三类耦合：

1. 头文件 / include 路径耦合
   - [build/android/Android.mk](/E:/Learn/my_program/all_my_hook/kanxue/Nook/build/android/Android.mk) 仍然包含：
   - `$(ROOT_PATH)/../Ninjector/jni`
   - `$(ROOT_PATH)/../../Ninjector/jni`

2. `symbi` 头文件和部分定义耦合
   - [server/symbi_injector_local.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/symbi_injector_local.cpp) 仍然直接包含：
   - `symbi/symbi_injector.h`
   - `symbi/symbi_stub.h`
   - `../../Ninjector/jni/common/log.h`

3. 历史兼容命名和路径残留
   - 例如 [server/ncore_fallback.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/ncore_fallback.cpp)
   - 以及 [server/server_runtime.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/server_runtime.cpp)
   - 里仍保留了 `/data/local/tmp/Ninjector/...` 这类兼容路径或默认值

## 所以，能不能“直接用 Nook 中的注入模块”？

可以，但准确说法是：

- 现在主逻辑其实已经主要在用 Nook 自己仓库里的注入模块
- 只是这些模块还没有彻底摆脱历史上的 Ninjector 头文件、symbi 组件和兼容路径

也就是说，不是“完全不能”，而是“已经做了一半，但还没收尾”。

## 为什么不能一步直接把外部 Ninjector 目录删掉

如果现在直接硬切，会有两个直接后果：

1. `symbi` 相关编译会先断
   - 因为头文件、stub 定义、日志宏还在借外部目录

2. 历史 fallback 和兼容路径会出现行为偏差
   - 某些旧逻辑仍默认指向 `Ninjector` 风格路径
   - 需要先在 Nook 内部统一收口后再删

所以正确顺序不是“先删外部依赖再看哪里炸”，而是：

1. 先把头文件和 `symbi` 定义迁进 Nook
2. 再清理 `/data/local/tmp/Ninjector/...` 兼容路径
3. 最后再把构建里的外部 include 去掉

## 下一步建议

下一步按这个顺序更稳：

1. 先把这轮 `spawn` 的稳定默认路径收敛清楚
   - `zygote-control` 继续作为实验路径
   - `symbi` 不作为默认稳定保底
   - 默认稳定保底应回到 embedded `legacy ncore`

2. 同时开始做 `Ninjector` 依赖内收
   - 把 `symbi` 头文件和必要实现迁到 Nook 仓库
   - 去掉 `Android.mk` 对外部 `Ninjector/jni` 的 include

3. 等依赖边界收干净后，再继续推进真正 Frida 风格的 `spawn`
   - 也就是稳定的 zygote 控制
   - 更可靠的 fd/memfd 装载
   - 最终再考虑彻底移除 `ncore`

## 当前判断

当前最务实的判断是：

- `attach`：已经基本进入 Nook 自主可控阶段
- `spawn`：稳定性仍依赖 `legacy ncore` 保底
- `Ninjector`：主逻辑已大部分内收，但还没有彻底从构建和 `symbi` 层面切干净

所以后面不是“从零开始重写注入模块”，而是“继续把已经迁进 Nook 的这套实现收口、去耦、稳定化”。
