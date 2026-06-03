# Nook 项目评价与下一步建议 (2026-04-25)

## 2026-04-27 Frida vs Nook 功能矩阵

| 维度 | Frida | Nook 当前状态 | 结论 |
|------|-------|----------------|------|
| JS runtime | 成熟 | QuickJS 跑通且稳定 | 基本对齐 |
| Process / Module / Memory | 完整 | 核心子集已可用 | 差距不大 |
| NativePointer / NativeFunction / NativeCallback | 完整 | 常用路径已覆盖 | 差距主要在边角和稳定性 |
| Interceptor.attach | 完整 | 已支持延迟安装、observer 模式、reload/unload | 已接近实用 |
| Interceptor.replace / revert | 完整 | 已实现 | 可用 |
| Thread.backtrace / DebugSymbol | 完整 | accurate/fuzzy 分流、缓存已加 | 还需继续做性能收口 |
| CLI / REPL / RPC | 成熟 | 主链路已打通 | 工程体验还可继续打磨 |
| Java.perform / use / implementation / callOriginal | 成熟 | 最小链路已跑通并真机验证 | 已进入可用阶段 |
| Java.choose / cast / retain / field / overload | 完整 | 常用子集已补齐并真机验证 | 正在逼近 Frida |
| Java ClassFactory / loader 工作流 | 完整 | `get/use/choose/cast/retain/$new` + `setClassLoader` 已补齐 | 这一块已明显接近 Frida |
| Java.openClassFile / registerClass / performNow | 完整 | 已补齐，host/device 已验证 | 这一块已基本对齐 Frida phase-1 目标 |
| Android 整体逆向体验 | 成熟 | Native 很强，Java 高阶生态仍弱于 Frida | 仍需继续补 Java 高层能力 |

### 当前结论

- 只看 Native 层，Nook 已经接近 Frida 的常用工作流。
- 只看 Java 基础能力，Nook 已经从 0 到 1，且 loader 生态最近补得比较完整。
- 真正的差距已经收敛到：
  - `Java.openClassFile(...)`
  - `Java.registerClass(...)`
  - 更成熟的 Java 高阶兼容性与工程稳定性

### 当前默认优先级

1. `Java.openClassFile(...)`
2. `Java.registerClass(...)`
3. spawn / zygote / ready 相关稳定性
4. Java overload / 类型转换 / 集合与数组边角
5. fuzzy backtrace 与 observer 语义继续收口

## 总体评价

基于当前 [code_review.md](/E:/Learn/my_program/all_my_hook/kanxue/Nook/docs/code_review.md) 和最近一轮真机验证，Nook 已经从“能跑通少量 smoke 的 Native Hook 原型”进入“可用于实际 Android 逆向工作的 Frida 风格动态插桩工具”阶段。

当前状态的关键判断：

- 三层架构已经打通：Host CLI / Device Server / Target Agent
- Native JS runtime 已具备较完整的 Frida 风格基础 API
- 延迟 Hook、REPL、RPC、脚本消息、replace / revert、backtrace、DebugSymbol 都已可用
- 当前最大的差距不再是 Native 基础设施，而是 Java Hook 能力与进一步的易用性收口

## 完成度评估

| 能力维度 | Frida | Nook 当前状态 | 完成度 |
|----------|-------|---------------|--------|
| JS 运行时 | V8/QuickJS | QuickJS | 100% |
| Module API | 完整 | 大部分已实现 | 90% |
| Process API | 完整 | 核心已实现 | 85% |
| Memory API | 完整 | 核心已实现 | 90% |
| NativePointer | 完整 | 核心已实现 | 90% |
| NativeFunction | 完整 | 已实现主要子集 | 85% |
| NativeCallback | 完整 | 已实现主要子集 | 85% |
| Interceptor.attach | 完整 | 已实现，含延迟安装 | 90% |
| Interceptor.replace | 完整 | 已实现 | 85% |
| 延迟 Hook | 完整 | 已实现 | 90% |
| Thread.backtrace | 完整 | 已实现主要形态与模式分流 | 80% |
| DebugSymbol | 完整 | 已实现并加入缓存 | 80% |
| Host CLI/REPL | 完整 | 已实现主要工作流 | 85% |
| Java Hook | 完整 | 尚未进入最小可用集 | 10% |

**整体完成度：约 85%**

这个阶段的 Nook 已经不是 demo。它已经能覆盖相当一部分 Frida 的 Native 工作流，只是还没有补上 Android 逆向中最关键的 Java 层能力。

## 当前亮点

### 1. 架构方向正确

```text
Host (Python SDK / CLI)
        ↓ TCP (adb forward)
Device Server (nook-server)
        ↓ Unix Socket
Target Process (Agent + QuickJS)
```

这套职责划分是对的：

- Host 负责用户交互、脚本装载、REPL、RPC
- Server 负责进程管理、spawn / attach / resume / detach、消息转发
- Agent 负责 runtime、hook dispatch、脚本执行

这是 Frida 风格能力继续扩展的正确底座。

### 2. 延迟 Hook 已经成为可用能力

```javascript
Interceptor.attach({ module: "libnative-lib.so", symbol: "..." }, callbacks)
```

当前已经支持：

- 目标 `.so` 未加载时脚本先成功装载
- `.so` 真正加载后自动完成 hook install
- attach / reload / unload / repl 场景都可复用

这解决了 Android Native Hook 最常见的时序问题，是当前阶段最有价值的能力之一。

### 3. Observer 模式成功解决了大卡顿

```javascript
Interceptor.attach(target, {
  blocking: false,
  onEnter(args) { ... }
});
```

这轮最关键的实际改进是：

- 新增 `blocking: false`
- 默认仍保留 `blocking: true`
- `DebugSymbol.fromAddress(...)` 增加 runtime 级缓存

结果是：

- 原本会导致明显 UI 卡顿的 backtrace-heavy observer hook，现在已经不再明显卡住 app
- 剩余成本主要集中在 fuzzy backtrace 的热路径，而不是整个 hook 机制本身

### 4. 验证覆盖比较扎实

已反复验证的工作流包括：

- spawn / attach / resume / detach / post / unload / repl
- 延迟 Hook / reload / unhook
- RPC 调用
- Module / Process / Memory / NativePointer API
- Interceptor replace / revert
- retval 替换
- UTF-16 指针操作
- Thread.backtrace + DebugSymbol

这说明核心链路已经打通，而且经过了真机反复验证。

## 当前问题与风险

### 1. Fuzzy backtrace 仍然偏重

虽然 app 已经不明显卡顿，但 `thread-backtrace-hook-fuzzy` 仍然比 accurate 模式慢，原因主要是：

- fuzzy 需要做栈扫描
- 候选地址更多
- 仍有符号化和字符串拼接开销

这是性能优化问题，不再是架构阻塞问题。

### 2. Observer 模式语义必须更明确

`blocking: false` 的本质是 observer-only：

- JS callback 会执行
- 但 hooked thread 不等待它完成
- 参数修改 / 返回值修改不会真正影响本次 native 调用

这一点需要在运行时和文档里都非常明确，否则用户会误以为：

```javascript
args[0].replace(...)
retval.replace(...)
```

在 observer 模式下也会生效。

当前建议是保留这个语义，并通过文档、示例和 warning 提示把它讲清楚，而不是偷偷做半同步或不稳定语义。

### 3. Java Hook 仍然是最大缺口

对于 Android 逆向，Native Hook 只能覆盖一部分需求。真正的主战场仍然是：

```javascript
Java.perform(function () {
  var Activity = Java.use("android.app.Activity");
  Activity.onCreate.implementation = function (bundle) {
    ...
  };
});
```

如果没有 Java Hook，Nook 很难在 Android 逆向场景里达到“Frida 级别可用”的体验。

### 4. 不建议现在分散到 iOS

虽然 Frida 是跨平台的，但对当前 Nook 来说：

- Android 仍然是主战场
- Java Hook 缺口远比 iOS 支持更紧迫
- iOS 移植成本高，当前阶段收益低

所以 iOS 应该明确放到长期路线，而不是当前阶段的主线任务。

## 修正后的下一步建议

## Phase 1: 稳定性与易用性收口 (1-2 周)

### 1. 完善 API 文档

- 为当前已实现的 JS API 补 JSDoc 风格文档
- 明确说明与 Frida 的差异
- 重点写清楚 `blocking: false` 的 observer-only 语义
- 增补更多最小可运行示例脚本

### 2. 增强错误提示与状态提示

- observer 模式下 mutation 被忽略时给出明确 warning
- 延迟 Hook 状态更清晰，例如 `pending` / `installed` / `failed`
- RPC timeout 和脚本错误给出更具体提示

### 3. 继续优化 fuzzy backtrace 热路径

- 限制默认输出量
- 缩减 smoke script 中的实时符号化和字符串拼接
- 后续再考虑 `maxFrames` 或更便宜的批量符号化接口

## Phase 2: Java Hook 最小可用集 (2-4 周)

这是下一阶段的核心目标。

建议最小实现优先级：

1. `Java.perform()`
2. `Java.use()`
3. 方法替换
4. 调用原方法

目标不是一步到位复刻整个 Frida Java API，而是先把最常用的核心路径跑通。

示例目标形态：

```javascript
Java.perform(function () {
  var LoginFragment = Java.use("com.demo.target.LoginFragment");
  LoginFragment.verifyPassword.implementation = function (text) {
    console.log("verifyPassword called:", text);
    return this.verifyPassword(text);
  };
});
```

## Phase 3: Java Hook 可用性增强 (后续 2-3 周)

在最小 Java Hook 路径可用后，再补：

- overload 选择
- 字段访问
- `Java.choose()` 或等价对象枚举
- 更稳定的 ART 兼容性处理

## Phase 4: Frida 常用工作流补齐 (持续)

这里不再重复已经实现的 API，而是补真正还缺的高层能力：

- trace / stalker 类能力
- 更强的符号化与 backtrace 能力
- 更完整的 CLI 工具链
- 更好的脚本开发体验和调试体验

## 里程碑建议

| 里程碑 | 目标 | 预估时间 |
|--------|------|----------|
| M1 | 稳定性 + 文档 + observer / fuzzy 收口 | 1-2 周 |
| M2 | `Java.perform()` 最小实现 | 2-3 周 |
| M3 | Java Hook 常用能力补齐 | 2-3 周 |
| M4 | 更完整的 Frida 工作流兼容 | 持续演进 |

## 总结

Nook 当前已经从“概念验证”进入“可用阶段”。

真正的结论不是“还差很多底层能力”，而是：

- Native / runtime / host 三层已经足够支撑后续扩展
- 当前 Native 侧最重要的问题已经从“能不能做”转成“稳不稳定、便不便宜、好不好用”
- 下一阶段必须聚焦 Java Hook，而不是继续横向铺开太多次要方向

如果 Phase 1 能顺利收口，并且在后续 1-2 个月内做出最小可用的 Java Hook，Nook 就会从“很强的 Native Frida-style 工具”进一步走向“真正可在 Android 逆向里长期使用的 Frida 替代 / 补充工具”。

## 2026-04-25 Java.perform 当前进展

当前最小 Java JS 路径已经进入设备构建产物：

- `Java.perform(fn)`
- `Java.use(className)`
- `Class.method.implementation = fn`
- `this.method.callOriginal(...)` 的最小桥接骨架

已经完成的部分：

- 桌面单测已覆盖 API surface、implementation 安装、最小 `callOriginal(...)` bridge
- Android `libnook.so` / `libnook-agent.so` / `nook-server` 已重新编译并推送
- 新增真机 smoke 脚本：`host/nook-py/java_perform_smoke.js`

当前真机 smoke 的语义要明确：

- 现在可以验证 `Java` 全局、wrapper 形状、`implementation` 安装路径
- 现在还不能把它当成“真实 Java 方法回调已经端到端进 JS”的完成态
- 真实 Java callback dispatch 仍是下一步设备侧集成任务

当前推荐验证命令：

```powershell
nook-cli attach com.demo.target -l E:\Learn\my_program\all_my_hook\kanxue\Nook\host\nook-py\java_perform_smoke.js --wait --usb
```

当前预期输出：

- `java-bindings:object:function:function`
- `java-wrapper:object:object:function`
- `java-implementation-installed`

---

## 2026-04-28 Nook vs Frida 深度对比分析

基于 [nook_frida_java_comparison_2026-04-28.md](nook_frida_java_comparison_2026-04-28.md) 的详细对比，给出以下分析。

### 一、完成度重新评估

对比 4 月 25 日的评估，Java 能力已经从 10% 跃升到约 75%。

| 能力维度 | 4/25 评估 | 4/28 实际 | 说明 |
|----------|-----------|-----------|------|
| Java.perform/ready/performNow | 部分 | **完整** | 包含 vm.perform 分层 |
| Java.use + 方法调用 | 部分 | **完整** | overload / implementation / callOriginal / $new |
| Java.cast / retain / dispose | 无 | **完整** | 含 weak cleanup |
| Java.choose | 无 | **完整** | factory.choose 已验证 |
| ClassFactory / loader 工作流 | 无 | **完整** | get/use/choose/cast/retain/$new/openClassFile |
| Java.registerClass | 无 | **可用** | interface/listener 代理已通 |
| Java.array | 无 | **可用** | primitive/object/multi-dimensional |
| Java.vm.getEnv + Env helper | 无 | **实用子集** | 缺 local ref / monitor |

**整体 Java 完成度：约 75%**

### 二、架构差异的关键影响

这是本次对比最重要的发现。

#### Nook 架构特点

```
┌─────────────────────────────────────────────────────┐
│  JS Script                                          │
│    └── Java.vm.getEnv() → Env wrapper              │
│          └── env.findClass() → C++ bridge          │
│                └── 每次调用都是独立的 JNI 进入      │
└─────────────────────────────────────────────────────┘
```

- **"runtime 代理型 Env"**：每次 `env.xxx()` 调用都重新进入 native/JNI 路径
- **优点**：行为稳定、桌面/设备测试统一、wrapper 生命周期可控
- **限制**：local ref 语义无法直接暴露

#### Frida 架构特点

```
┌─────────────────────────────────────────────────────┐
│  JS Script                                          │
│    └── JNIEnv* 直接暴露                             │
│          └── JS 直接解析 JNI vtable                 │
│                └── 低层能力直接可用                  │
└─────────────────────────────────────────────────────┘
```

- **"JS 直连 JNI"**：更低的包装层级
- **优点**：低层暴露快、新增 JNI helper 成本低
- **限制**：语义更原始，需要上层约束

#### 架构决定的能力边界

| 能力 | Nook | Frida | 原因 |
|------|------|-------|------|
| newGlobalRef / deleteGlobalRef | ✅ 可做 | ✅ | 全局引用生命周期不依赖调用帧 |
| newWeakGlobalRef / deleteWeakGlobalRef | ✅ 可做 | ✅ | 同上 |
| newLocalRef / deleteLocalRef | ❌ 不能直接做 | ✅ | local ref 生命周期绑定 JNI 调用帧 |
| pushLocalFrame / popLocalFrame | ❌ 不能直接做 | ✅ | 同上 |
| monitorEnter / monitorExit | ✅ 可做 | ✅ | 不依赖帧局部语义 |

**结论**：Nook 不是"还没实现 local ref"，而是**架构决定不能按 Frida 方式实现**。如果要支持，需要设计新的执行模型（如批量 JNI 调用帧）。

### 三、当前真正的差距

基于对比文档，差距已收敛到以下几点：

#### 1. Env 层缺失的稳定原语

| 原语 | 优先级 | 说明 |
|------|--------|------|
| `monitorEnter()` / `monitorExit()` | **高** | 常用，且架构可支持 |
| `getSuperclass()` | 中 | 偶尔需要 |
| `isAssignableFrom()` | 中 | 类型检查 |
| `ensureLocalCapacity()` | 低 | 架构限制，意义有限 |
| `newLocalRef()` / `deleteLocalRef()` | **不建议** | 架构不支持 |

#### 2. registerClass 完整语义

当前 `Java.registerClass` 已可用于 interface/listener 代理，但还不是完整动态类系统：
- 缺 richer spec semantics
- 缺 superclass/implements 完整组合
- 缺 field 定义

#### 3. Java.array 完整对象模型

当前够用，但不是 Frida 的 live mutable array wrapper：
- 当前更像"创建后传递"
- 缺 mutation / reread 语义
- 缺更完整的 covariance 处理

#### 4. 工程稳定性

- spawn / zygote / ready 时序在某些 ROM 上可能仍有边角
- overload 推断在复杂重载场景可能有遗漏
- 大量 Java 调用时的性能还需观察

### 四、修正后的下一步优先级

基于架构分析，建议如下：

#### 立即可做（架构支持）

| 优先级 | 任务 | 预估 |
|--------|------|------|
| P0 | `monitorEnter()` / `monitorExit()` | 1-2 天 |
| P1 | `getSuperclass()` / `isAssignableFrom()` | 1 天 |
| P1 | `registerClass` spec 扩展（field 定义） | 2-3 天 |
| P2 | `Java.array` mutation 语义 | 2-3 天 |

#### 需要架构决策（不建议当前做）

| 任务 | 问题 |
|------|------|
| `newLocalRef()` / `deleteLocalRef()` | 需要批量调用帧模型，复杂度高 |
| `pushLocalFrame()` / `popLocalFrame()` | 同上 |

#### 持续优化

| 任务 | 说明 |
|------|------|
| spawn/ready 稳定性 | 多 ROM 测试 |
| overload 边角 | 复杂重载场景 |
| 文档 + 示例 | 说明与 Frida 的差异 |

### 五、里程碑更新

| 里程碑 | 目标 | 状态 |
|--------|------|------|
| M1 | Native Hook + Observer + Backtrace | ✅ 已完成 |
| M2 | Java.perform/use/implementation 最小可用 | ✅ 已完成 |
| M3 | ClassFactory + choose + cast + retain + registerClass | ✅ 已完成 |
| M4 | Env helper 补齐 (monitor / superclass) | **进行中** |
| M5 | registerClass 完整语义 + array mutation | 待开始 |
| M6 | 文档 + 稳定性 + 工程收口 | 持续 |

### 六、总结

Nook 在 Java 能力上的进展远超预期。从 4 月 25 日的 10% 到现在的 75%，已经具备了：
- 完整的 perform/use/implementation/callOriginal 工作流
- 完整的 ClassFactory/loader 生态
- 可用的 registerClass/array/Env helper

**最重要的发现**是架构差异：Nook 的 "runtime 代理型 Env" 决定了某些 Frida 能力（如 local ref）不能直接照搬。这不是 bug，而是设计选择的结果。

**下一步建议**：
1. 补 `monitorEnter/monitorExit`（高价值、低成本）
2. 扩展 `registerClass` 语义
3. 完善文档，明确说明与 Frida 的架构差异
4. 不要尝试硬做 `local ref`，除非愿意重构 Env 执行模型
## 2026-04-29 correction

This addendum supersedes the earlier recommendation in this file that
`monitorEnter/monitorExit` were "high-value, low-cost" helpers.

Device validation showed:

- `monitorEnter(...)` succeeds
- `monitorExit(...)` fails on the same wrapper when executed through the next `env.xxx()` helper call

Architecture conclusion:

- this is not a missing implementation detail
- this is an `Env` execution-model boundary
- the same boundary class now includes:
  - local refs
  - local frames
  - monitor enter/exit pairs

Updated recommendation:

1. Continue with helpers that remain safe across independent JNI re-entry.
2. Do not expose `monitorEnter/monitorExit` again under the current `Env` architecture.
3. Only revisit monitor support after redesigning the `Env` execution model.

## 2026-04-29 build and injection correction

This addendum records a separate issue discovered while validating
`Java.enumerateClassLoaders(...)` on device.

Observed symptoms:

- source code already contained the new Java bootstrap
- host-side tests also passed against the new bootstrap
- but device-side scripts sometimes only saw a truncated `Java` object:
  - `Java.ready` existed
  - `Java.enumerateClassLoaders` was `undefined`
  - `Java.ClassFactory` was `undefined`
  - `Java._invokeResolverVersion` was `undefined`

Root cause:

- the problem was not the JS runtime logic
- the problem was not `Java.enumerateClassLoaders(...)`
- the device had been updated with a manually linked `libnook-agent.so`
  used during diagnosis
- that manual artifact contained the expected bootstrap strings, but it did
  not work reliably with the current `inject_so_by_pid` attach path
- once pushed to device, `attach` failed with:
  - `error: inject_so_by_pid failed`

Important conclusion:

- for this project, a "contains the right strings" manual shared library is
  not automatically a valid delivery artifact
- the correct deployment artifact is still the standard Android build output
  produced by `ndk-build`

Validated recovery:

1. replace the device-side `/data/local/tmp/nook/libnook-agent.so`
   with the standard `ndk-build` output from:
   `libs/arm64-v8a/libnook-agent.so`
2. re-run:
   `nook-cli attach com.demo.target -l host/nook-py/java_enumerate_class_loaders_smoke.js --wait --usb`
3. observed result:
   - `attach ok`
   - script load succeeded
   - `Java.enumerateClassLoaders(...)` was present
   - loader enumeration returned:
     - `dalvik.system.PathClassLoader`
     - `java.lang.BootClassLoader`
     - `dalvik.system.PathClassLoader`

Implications for next steps:

1. do not use the manual linked `build/manual_libnook-agent.so` as a runtime
   deployment artifact
2. keep device validation pinned to the standard `ndk-build` output
3. if bootstrap/API mismatches reappear, first verify the exact on-device
   `libnook-agent.so` origin before debugging JS runtime behavior
4. longer-term, fix the Android build pipeline so diagnostic relink work is
   never needed to validate current runtime changes
