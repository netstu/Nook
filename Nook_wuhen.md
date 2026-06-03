# Nook 无痕 Hook 方案移植设计文档（Root-Only）

版本：0.2  
状态：设计稿  
范围：面向当前 `Nook` 仓库的集成设计，不改代码，仅定设计。

## 1. 背景

当前仓库已经具备三条主线：
- native hook：`src/native_hook/inline_hook/`
- Java hook：`src/java_hook/`
- 运行时与脚本桥接：`src/agent_runtime/`

现有实现以 `mprotect + trampoline` 为主，隐蔽性有限。本设计目标是在**仅要求 root、无内核模块**的前提下，增加一套**可选**的高隐蔽 hook 后端，并保留现有 `classic` 路径作为回退。

## 2. 目标

- 优先隐蔽，尽量稳定。
- 不改变上层 API 语义。
- native / Java 都可按策略选择 `classic` 或 `stealth-root-only`。
- 失败必须可回退，不能把目标进程置于半安装状态。

## 3. 非目标

- 不实现内核级 `PTE/UXN shadow page`。
- 不实现 `Ghost Memory`。
- 不承诺对外部进程检测的完全虚拟化。
- 不承诺所有 ROM / ART 版本同等可用。

## 4. 总体架构

采用“双后端 + 统一调度”：

- `classic`：现有 inline / plt / Java 路径。
- `stealth-root-only`：root-only 替代后端。
- `Hook Strategy Layer`：统一选路，不让各模块自行决定后端。

### 4.1 结构图

```text
Host / Server
    |
    v
agent_runtime
    |
    +--> Hook Strategy Layer
    |        |
    |        +--> classic-inline / classic-plt / classic-java
    |        +--> stealth-page-redirect
    |        +--> stealth-hwbp
    |        +--> java-entry-redirect
    |
    +--> Native Stealth Backend
    |
    +--> Java Stealth Backend
```

### 4.2 统一调度层

职责：
- 根据 hook 类型、目标页属性、root 状态、风险等级选择后端。
- 输出安装结果、失败原因、回退结果。

建议位置：
- `src/framework/` 与 `src/agent_runtime/` 之间的策略层。

### 4.3 Native Stealth Backend

建议新增：
- `src/native_hook/stealth_hook/`

子模块：
- `page_redirect_manager`
- `dbi_relocator`
- `signal_gate`
- `maps_masking`
- `hwbp_manager`
- `stealth_alloc`

职责：
- 页级执行拦截。
- 少量关键点 HWBP。
- 伪装 `maps` / `sigaction` / fd 可见性。
- 承载克隆页、thunk、回调页。

### 4.4 Java Stealth Backend

建议新增：
- `src/java_hook/stealth_java/`

子模块：
- `art_entry_router`
- `java_bridge_redirect`
- `java_stealth_policy`

职责：
- 优先入口级重定向。
- 能走 native bridge 的不走裸 patch。
- 不稳定时回退现有 Java hook。

### 4.5 Runtime Stealth Services

建议放在：
- `src/agent_runtime/`

职责：
- 维护 stealth 状态表。
- 管理 ownership、offset map、线程局部递归保护。
- 向 JS 暴露最小必要状态。

### 4.6 Host / Server Awareness

`server/` 与 `host/nook-py/` 只需知道：
- 是否允许 stealth
- 当前安装结果
- 失败原因

不需要理解底层细节。

## 5. Native 侧策略

### 5.1 选路

优先级建议：
1. `stealth-page-redirect`
2. `stealth-hwbp`
3. `classic-inline`
4. `classic-plt`

### 5.2 数据流

`NookInlineHook*` / `Interceptor.attach/replace`  
→ `Hook Strategy Layer`  
→ 选择后端  
→ 安装 hook  
→ 记录 hook ownership  
→ 失败则回退 classic

### 5.3 关键实现约束

- 页级拦截必须支持线程安全与递归保护。
- 同页多函数要避免误伤。
- 不能破坏当前 `detach/unload/shutdown` 生命周期。

### 5.4 与当前代码的对应关系

- `src/native_hook/inline_hook/inline_hook_impl.cpp`：现有 classic inline 主线。
- `src/native_hook/core/runtime_patch.cpp`：当前写页与恢复页的通用基础。
- `src/agent_runtime/nook_native_js_bridge.cpp`：JS `Interceptor` 的 native 入口。

## 6. Java 侧策略

### 6.1 原则

Java 侧不直接复用 native 的页级思路，而是优先做入口级路由：
- 能通过 `ArtMethod` 路由解决的，优先路由。
- 必须落到 native 的，交给 native stealth backend。
- 版本不稳定时，回退现有 Java hook。

### 6.2 数据流

`Java.perform` / `Java.use`  
→ `Java Stealth Backend`  
→ `ArtMethod` 入口级重定向或 native bridge  
→ 回调 `agent_runtime`

### 6.3 与当前代码的对应关系

- `src/java_hook/JavaHook.cpp`：现有 Java hook 主流程。
- `src/java_hook/router/hook_engine_art.c`：现有 ART router 基础。
- `src/java_hook/router/hook_engine_mem.c`：现有 `wxshadow` 试验路径。

## 7. 接口草案

### 7.1 策略输入

建议抽象为：
- hook 类型
- 目标地址 / 模块 / 符号
- 是否允许 stealth
- 是否允许信号后端
- 是否允许 HWBP
- 是否允许回退 classic

### 7.2 安装结果

建议统一返回：
- `backend_used`
- `status`
- `reason`
- `hook_id`
- `deferred`

### 7.3 内部服务

建议新增内部服务能力：
- `SelectHookBackend(...)`
- `InstallStealthHook(...)`
- `InstallClassicHook(...)`
- `RollbackHook(...)`

## 8. 回退策略

必须满足：
- stealth 安装失败立即回退。
- 目标页不适合页级方案时回退。
- Java 入口布局不稳定时回退。
- 任何失败都不能让目标进程崩溃。

### 8.1 失败流程

```text
请求安装
  -> 策略选择 stealth
  -> stealth 安装失败
  -> 回退 classic
  -> classic 成功则返回
  -> classic 失败则返回错误
```

### 8.2 卸载流程

```text
请求卸载
  -> 按 hook_id / target 查找记录
  -> 若为 stealth，先撤销 stealth 状态
  -> 若为 classic，恢复原始补丁
  -> 清理 ownership
```

## 9. 风险

- `SIGSEGV` 递归和重入。
- 页级影响扩大。
- `maps` / `sigaction` 伪装与 Nook 自身冲突。
- Java / Native stealth 状态不同步。

## 10. 分阶段落地

### Phase 1

- 策略层。
- native stealth backend 骨架。
- 回退链路。

### Phase 2

- 页级 redirect。
- `HWBP` 补充。
- Java 入口级重定向。

### Phase 3

- 伪装服务完善。
- 观测与诊断增强。

## 11. 验收标准

- 可按策略切换 `classic` / `stealth-root-only`。
- stealth 失败可无感回退。
- 现有 API 语义不变。
- `detach/unload/shutdown` 正常。

## 12. 当前仓库落点

- `src/native_hook/inline_hook/inline_hook_impl.cpp`：现有 native 主线。
- `src/java_hook/router/hook_engine*.c`：现有 Java router 与 stealth 试验路径。
- `src/agent_runtime/js_runtime.cpp`：JS 渗透入口。
- `server/`、`host/nook-py/`：只做最小策略可见。

