# Nook 项目进展总结与下一步建议 (2026-04-29)

## 一、本轮关键发现

### 1. Env 执行模型边界进一步明确

4/28 的分析认为 `monitorEnter/monitorExit` 是"高价值、低成本"的 helper。4/29 的真机验证推翻了这个结论。

**实际观察**：
```
monitorEnter(obj)  → 成功
monitorExit(obj)   → 失败（在下一次 env.xxx() 调用中执行时）
```

**根因**：Nook 的 "runtime 代理型 Env" 模型下，每次 `env.xxx()` 都是独立的 JNI 进入。`monitorEnter` 获取的 monitor 锁在当前 JNI 调用返回后就已经处于"不一致"状态，后续 `monitorExit` 无法正确释放。

**边界类扩展**：

| 能力类型 | 状态 | 原因 |
|----------|------|------|
| `newLocalRef` / `deleteLocalRef` | ❌ 不可做 | local ref 生命周期绑定 JNI 调用帧 |
| `pushLocalFrame` / `popLocalFrame` | ❌ 不可做 | 同上 |
| `monitorEnter` / `monitorExit` | ❌ 不可做 | monitor 状态依赖同一 JNI 调用帧 |
| `newGlobalRef` / `deleteGlobalRef` | ✅ 可做 | 全局引用不依赖调用帧 |
| `newWeakGlobalRef` / `deleteWeakGlobalRef` | ✅ 可做 | 同上 |

这是架构边界，不是实现缺陷。

### 2. 构建产物一致性问题

验证 `Java.enumerateClassLoaders` 时发现设备上的 `libnook-agent.so` 不是标准 ndk-build 输出。

**症状**：
- 源码和 host 测试都包含完整 Java bootstrap
- 设备侧 `Java.enumerateClassLoaders` 为 `undefined`
- attach 失败：`error: inject_so_by_pid failed`

**根因**：
- 设备上残留了手动链接的诊断版本 `libnook-agent.so`
- 该版本虽然"包含正确的字符串"，但与 `inject_so_by_pid` 注入路径不兼容

**结论**：
- "包含正确代码"不等于"可正确部署"
- 必须使用标准 `ndk-build` 输出：`libs/arm64-v8a/libnook-agent.so`

### 3. Java.enumerateClassLoaders 验证通过

修复构建产物后，验证成功：
```
attach ok
Java.enumerateClassLoaders(...) present
loaders:
  - dalvik.system.PathClassLoader
  - java.lang.BootClassLoader
  - dalvik.system.PathClassLoader
```

---

## 二、架构边界的完整图景

经过多轮真机验证，Nook 的 Env 执行模型边界已经完全明确：

```
┌─────────────────────────────────────────────────────────────┐
│                    Nook Env 执行模型                         │
├─────────────────────────────────────────────────────────────┤
│  JS: env.findClass("...")                                   │
│       ↓                                                     │
│  C++ bridge: 进入 JNI → 执行 → 返回 JS                       │
│       ↓                                                     │
│  JS: env.newGlobalRef(...)                                  │
│       ↓                                                     │
│  C++ bridge: 重新进入 JNI → 执行 → 返回 JS                   │
│                                                             │
│  每次 env.xxx() 都是独立的 JNI 进入/退出                     │
└─────────────────────────────────────────────────────────────┘
```

**可安全跨调用的能力**：
- 全局引用管理
- 类查找
- 对象类型检查
- 字符串操作
- 异常检查/清除

**不可跨调用的能力**：
- local ref（生命周期绑定调用帧）
- local frame（同上）
- monitor lock（状态绑定调用帧）

---

## 三、当前完成度评估

| 能力维度 | 完成度 | 说明 |
|----------|--------|------|
| Native Hook | 95% | 核心完整，边角待打磨 |
| Java.perform/use/implementation | 90% | 主路径完整 |
| Java.choose/cast/retain | 90% | 已验证 |
| ClassFactory/loader | 90% | 已验证 |
| Java.registerClass | 75% | interface/listener 可用 |
| Java.array | 70% | 基础类型完整，高级语义待补 |
| Env helper | 60% | 安全子集已补，边界已明确 |
| 文档 | 40% | 需要补充架构差异说明 |

**整体完成度：约 80%**

---

## 四、修正后的下一步优先级

### 不再建议做

| 任务 | 原因 |
|------|------|
| `monitorEnter` / `monitorExit` | 架构不支持 |
| `newLocalRef` / `deleteLocalRef` | 架构不支持 |
| `pushLocalFrame` / `popLocalFrame` | 架构不支持 |

除非重新设计 Env 执行模型（如批量 JNI 调用帧、闭包执行等），否则这些能力不应列入计划。

### 当前可做（架构安全）

| 优先级 | 任务 | 预估 | 说明 |
|--------|------|------|------|
| P0 | `getSuperclass()` | 0.5 天 | 常用，跨调用安全 |
| P0 | `isAssignableFrom()` | 0.5 天 | 类型检查，跨调用安全 |
| P1 | `registerClass` field 定义 | 2 天 | 扩展 spec 语义 |
| P1 | `Java.array` length/get/set | 2 天 | 基础 mutation |
| P2 | spawn/ready 稳定性测试 | 持续 | 多 ROM 验证 |

### 文档优先

| 优先级 | 任务 | 说明 |
|--------|------|------|
| **P0** | 架构差异文档 | 说明 Nook vs Frida 的 Env 模型差异 |
| P1 | API 参考文档 | 已实现能力的 JSDoc |
| P2 | 示例脚本集 | 覆盖常见用例 |

---

## 五、里程碑更新

| 里程碑 | 目标 | 状态 |
|--------|------|------|
| M1 | Native Hook + Observer + Backtrace | ✅ 完成 |
| M2 | Java.perform/use/implementation | ✅ 完成 |
| M3 | ClassFactory + choose + cast + retain | ✅ 完成 |
| M4 | Java.enumerateClassLoaders 验证 | ✅ 完成 |
| M5 | Env 执行模型边界明确 | ✅ 完成 |
| M6 | 安全 Env helper 补齐 | **进行中** |
| M7 | registerClass/array 语义扩展 | 待开始 |
| M8 | 文档 + 稳定性收口 | 待开始 |

---

## 六、结论

### 1. 架构边界已完全明确

Nook 的 "runtime 代理型 Env" 模型有明确的能力边界。`monitorEnter/Exit`、`local ref`、`local frame` 都不可做，这是设计选择而非实现缺陷。

### 2. Java 能力已进入实用阶段

从 4/25 的 10% 到现在的约 80%，Nook 已具备：
- 完整的 perform/use/implementation 工作流
- 完整的 ClassFactory/loader 生态
- 可用的 registerClass/array

### 3. 下一阶段重心

| 重心 | 说明 |
|------|------|
| **文档优先** | 架构差异是最重要的用户预期管理 |
| 安全 Env helper | 只做跨调用安全的能力 |
| registerClass/array | 扩展语义，不追求完整 Frida 对齐 |
| 稳定性 | 多 ROM 测试，构建流程规范化 |

### 4. 不要做的事

| 事项 | 原因 |
|------|------|
| 硬做 local ref / monitor | 架构不支持，强行做会引入不稳定行为 |
| 追求完整 Frida 对齐 | Nook 有自己的架构边界，接受差异 |
| 使用非标准构建产物 | 必须使用 ndk-build 输出 |

---

## 七、总结

Nook 已经从"Frida 风格 Native Hook 工具"进化为"具备实用 Java Hook 能力的动态插桩框架"。

最重要的收获不是功能堆积，而是**架构边界的明确**：
- 知道什么能做（全局引用、类查找、对象检查）
- 知道什么不能做（local ref、monitor、local frame）
- 知道为什么（runtime 代理型 Env 的独立调用帧模型）

下一阶段的核心任务是**文档**和**稳定性**，而不是继续铺开新功能。让已有能力变得可靠、可理解、可维护，比追加更多边角能力更有价值。
