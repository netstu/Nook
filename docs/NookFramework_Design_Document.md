# Nook Framework 需求与设计文档

> 基于 Nook 构建类 Frida 动态插桩框架

**版本**: 1.0  
**日期**: 2026-04-22  
**状态**: Draft

---

## 目录

1. [项目概述](#1-项目概述)
2. [需求分析](#2-需求分析)
3. [系统架构](#3-系统架构)
4. [模块详细设计](#4-模块详细设计)
5. [API 设计](#5-api-设计)
6. [通信协议设计](#6-通信协议设计)
7. [技术选型](#7-技术选型)
8. [实现路线图](#8-实现路线图)
9. [风险与挑战](#9-风险与挑战)
10. [附录](#10-附录)

---

## 1. 项目概述

### 1.1 项目背景

Nook 是一个 Android Native Hook 框架，已实现 Java Hook、PLT Hook 和 Inline Hook (arm64) 的核心能力。当前 Nook 以静态编译的 payload 形式工作，缺乏动态脚本能力和远程调试支持。

本项目旨在将 Nook 扩展为一个完整的动态插桩框架（代号 **Nook Framework**），提供类似 Frida 的使用体验。

### 1.2 项目目标

| 目标 | 描述 |
|------|------|
| **动态性** | 支持运行时加载/卸载脚本，无需重新编译 |
| **远程调试** | 主机端控制设备端 Agent，实时交互 |
| **易用性** | 提供 JavaScript API 和 Python 客户端 |
| **完整性** | 覆盖 Java Hook、Native Hook、内存操作、模块枚举等核心场景 |
| **可扩展性** | 模块化架构，便于添加新功能 |

### 1.3 项目范围

**包含 (In Scope)**:
- Android arm64-v8a 平台支持
- JavaScript 脚本引擎
- 主机-设备通信层
- 进程注入能力
- Java/Native Hook API
- Memory/Module/Process API
- CLI 工具和 Python Binding

**不包含 (Out of Scope, Phase 1)**:
- iOS 支持
- arm32 / x86 架构
- Stalker 代码追踪
- 反检测/隐藏能力

### 1.4 术语定义

| 术语 | 定义 |
|------|------|
| **Agent** | 运行在目标进程内的动态库，执行实际 Hook 操作 |
| **Host** | 主机端控制程序（PC/Mac/Linux） |
| **Session** | 一次 attach/spawn 建立的连接会话 |
| **Script** | 用户编写的 JavaScript 代码 |
| **Gadget** | 可嵌入 APK 的 Agent 变体 |

---

## 2. 需求分析

### 2.1 功能需求

#### FR-001: 进程管理

| ID | 需求 | 优先级 | 描述 |
|----|------|--------|------|
| FR-001-1 | 进程枚举 | P0 | 列出设备上所有运行进程 |
| FR-001-2 | Attach 模式 | P0 | 附加到已运行的进程 |
| FR-001-3 | Spawn 模式 | P0 | 启动并注入目标应用 |
| FR-001-4 | 进程 Detach | P0 | 安全断开连接 |
| FR-001-5 | 应用枚举 | P1 | 列出已安装应用 |

#### FR-002: 脚本引擎

| ID | 需求 | 优先级 | 描述 |
|----|------|--------|------|
| FR-002-1 | JS 执行 | P0 | 执行 JavaScript 代码 |
| FR-002-2 | 脚本加载 | P0 | 从主机加载脚本到 Agent |
| FR-002-3 | 热重载 | P1 | 运行时替换脚本 |
| FR-002-4 | 错误处理 | P0 | JS 异常捕获和上报 |
| FR-002-5 | console.log | P0 | 日志输出到主机 |

#### FR-003: Java Hook

| ID | 需求 | 优先级 | 描述 |
|----|------|--------|------|
| FR-003-1 | 方法 Hook | P0 | Hook Java 方法，获取参数和返回值 |
| FR-003-2 | 方法替换 | P0 | 完全替换方法实现 |
| FR-003-3 | 构造函数 Hook | P0 | Hook 类构造函数 |
| FR-003-4 | 方法调用 | P1 | 主动调用 Java 方法 |
| FR-003-5 | 类枚举 | P1 | 枚举已加载的类 |
| FR-003-6 | 实例操作 | P1 | 获取/修改对象字段 |

#### FR-004: Native Hook

| ID | 需求 | 优先级 | 描述 |
|----|------|--------|------|
| FR-004-1 | Inline Hook | P0 | 通过地址或符号进行 Inline Hook |
| FR-004-2 | PLT Hook | P0 | Hook 导入函数 |
| FR-004-3 | 延迟 Hook | P0 | 模块加载后自动 Hook |
| FR-004-4 | 参数访问 | P0 | 读取/修改函数参数 |
| FR-004-5 | 返回值修改 | P0 | 修改函数返回值 |
| FR-004-6 | Unhook | P0 | 移除 Hook |

#### FR-005: 内存操作

| ID | 需求 | 优先级 | 描述 |
|----|------|--------|------|
| FR-005-1 | 内存读取 | P0 | 读取任意地址内存 |
| FR-005-2 | 内存写入 | P0 | 写入任意地址内存 |
| FR-005-3 | 内存分配 | P0 | 在目标进程分配内存 |
| FR-005-4 | 内存保护 | P1 | 修改内存页属性 |
| FR-005-5 | 内存扫描 | P2 | 模式搜索 |

#### FR-006: 模块与符号

| ID | 需求 | 优先级 | 描述 |
|----|------|--------|------|
| FR-006-1 | 模块枚举 | P0 | 列出所有加载的模块 |
| FR-006-2 | 导出查找 | P0 | 按名称查找导出符号 |
| FR-006-3 | 导出枚举 | P1 | 枚举模块所有导出 |
| FR-006-4 | 导入枚举 | P1 | 枚举模块所有导入 |
| FR-006-5 | 基址获取 | P0 | 获取模块加载基址 |

#### FR-007: 通信与交互

| ID | 需求 | 优先级 | 描述 |
|----|------|--------|------|
| FR-007-1 | 双向通信 | P0 | Host 和 Agent 双向消息传递 |
| FR-007-2 | RPC 调用 | P1 | 主机调用 Agent 导出的函数 |
| FR-007-3 | 事件推送 | P0 | Agent 主动推送事件到主机 |
| FR-007-4 | 二进制传输 | P1 | 高效传输二进制数据 |

### 2.2 非功能需求

| ID | 需求 | 描述 | 指标 |
|----|------|------|------|
| NFR-001 | 性能 | Hook 开销 | 单次 Hook 调用 < 1μs 额外开销 |
| NFR-002 | 稳定性 | Agent 崩溃隔离 | 脚本异常不导致目标进程崩溃 |
| NFR-003 | 体积 | Agent 大小 | < 2MB (不含脚本) |
| NFR-004 | 兼容性 | Android 版本 | Android 8.0 - 14 (API 26-34) |
| NFR-005 | 延迟 | 通信延迟 | 本地 < 10ms, USB < 50ms |

### 2.3 用户故事

**US-001: 安全研究员分析 App**
```
作为安全研究员，
我想要 attach 到目标 App 并 Hook 其加密函数，
以便分析其加密算法和密钥。
```

**US-002: 开发者调试 Native 代码**
```
作为 App 开发者，
我想要在运行时 Hook Native 函数并打印参数，
以便调试难以复现的问题。
```

**US-003: 逆向工程师绕过检测**
```
作为逆向工程师，
我想要 Hook Java 层的签名校验函数并修改返回值，
以便绕过应用的完整性检测。
```

---

## 3. 系统架构

### 3.1 整体架构

```
┌─────────────────────────────────────────────────────────────────────┐
│                           Host (PC/Mac)                              │
├─────────────────────────────────────────────────────────────────────┤
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────────────┐  │
│  │   CLI Tool  │  │ Python SDK  │  │     Node.js SDK (可选)      │  │
│  │  nook-cli   │  │  nook-py    │  │        nook-node            │  │
│  └──────┬──────┘  └──────┬──────┘  └──────────────┬──────────────┘  │
│         │                │                        │                  │
│         └────────────────┼────────────────────────┘                  │
│                          ▼                                           │
│  ┌───────────────────────────────────────────────────────────────┐  │
│  │                      Nook Core Library                         │  │
│  │  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────┐    │  │
│  │  │   Session   │  │   Device    │  │   Script Compiler   │    │  │
│  │  │   Manager   │  │   Manager   │  │   (可选, TypeScript) │    │  │
│  │  └─────────────┘  └─────────────┘  └─────────────────────┘    │  │
│  └───────────────────────────┬───────────────────────────────────┘  │
│                              │                                       │
│                              ▼                                       │
│  ┌───────────────────────────────────────────────────────────────┐  │
│  │                    Transport Layer                             │  │
│  │         USB (ADB forward)  /  TCP  /  Unix Socket              │  │
│  └───────────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────────┘
                               │
                               │ Network / USB
                               ▼
┌─────────────────────────────────────────────────────────────────────┐
│                        Android Device                                │
├─────────────────────────────────────────────────────────────────────┤
│  ┌───────────────────────────────────────────────────────────────┐  │
│  │                      Nook Server                               │  │
│  │  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────┐    │  │
│  │  │   Injector  │  │   Process   │  │   Connection Pool   │    │  │
│  │  │   Engine    │  │   Manager   │  │                     │    │  │
│  │  └─────────────┘  └─────────────┘  └─────────────────────┘    │  │
│  └───────────────────────────┬───────────────────────────────────┘  │
│                              │                                       │
│                              │ ptrace / dlopen                       │
│                              ▼                                       │
│  ┌───────────────────────────────────────────────────────────────┐  │
│  │                    Target Process                              │  │
│  │  ┌─────────────────────────────────────────────────────────┐  │  │
│  │  │                    Nook Agent                            │  │  │
│  │  │  ┌──────────────────────────────────────────────────┐   │  │  │
│  │  │  │              JavaScript Engine (QuickJS)          │   │  │  │
│  │  │  │  ┌────────────────────────────────────────────┐  │   │  │  │
│  │  │  │  │              User Script                    │  │   │  │  │
│  │  │  │  └────────────────────────────────────────────┘  │   │  │  │
│  │  │  └──────────────────────────────────────────────────┘   │  │  │
│  │  │                          │                               │  │  │
│  │  │                          ▼                               │  │  │
│  │  │  ┌──────────────────────────────────────────────────┐   │  │  │
│  │  │  │                 Binding Layer                     │   │  │  │
│  │  │  │   Java API │ Interceptor │ Memory │ Module │ ...  │   │  │  │
│  │  │  └──────────────────────────────────────────────────┘   │  │  │
│  │  │                          │                               │  │  │
│  │  │                          ▼                               │  │  │
│  │  │  ┌──────────────────────────────────────────────────┐   │  │  │
│  │  │  │                 Nook Core (现有)                  │   │  │  │
│  │  │  │  Java Hook │ PLT Hook │ Inline Hook │ Utilities   │   │  │  │
│  │  │  └──────────────────────────────────────────────────┘   │  │  │
│  │  └─────────────────────────────────────────────────────────┘  │  │
│  └───────────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────────┘
```

### 3.2 组件职责

| 组件 | 位置 | 职责 |
|------|------|------|
| **CLI Tool** | Host | 命令行用户界面 |
| **Python SDK** | Host | Python 编程接口 |
| **Session Manager** | Host | 管理连接会话生命周期 |
| **Device Manager** | Host | 设备发现和管理 |
| **Transport Layer** | Host/Device | 通信抽象层 |
| **Nook Server** | Device | 设备端守护进程 |
| **Injector Engine** | Device | 进程注入实现 |
| **Nook Agent** | Target Process | 注入到目标的核心组件 |
| **JS Engine** | Agent | JavaScript 执行环境 |
| **Binding Layer** | Agent | JS 到 Native 的桥接层 |
| **Nook Core** | Agent | 现有 Hook 实现 |

### 3.3 数据流

#### 3.3.1 Attach 流程

```
┌──────┐      ┌────────┐      ┌───────────┐      ┌────────┐      ┌───────┐
│ User │      │  CLI   │      │  Session  │      │ Server │      │ Agent │
└──┬───┘      └───┬────┘      └─────┬─────┘      └───┬────┘      └───┬───┘
   │              │                 │                │               │
   │ nook -U -n   │                 │                │               │
   │  com.app     │                 │                │               │
   │─────────────>│                 │                │               │
   │              │                 │                │               │
   │              │  attach(pid)    │                │               │
   │              │────────────────>│                │               │
   │              │                 │                │               │
   │              │                 │  inject(pid)   │               │
   │              │                 │───────────────>│               │
   │              │                 │                │               │
   │              │                 │                │  ptrace +     │
   │              │                 │                │  dlopen       │
   │              │                 │                │──────────────>│
   │              │                 │                │               │
   │              │                 │                │  Agent Ready  │
   │              │                 │                │<──────────────│
   │              │                 │                │               │
   │              │                 │  session_id    │               │
   │              │                 │<───────────────│               │
   │              │                 │                │               │
   │              │  Session OK     │                │               │
   │              │<────────────────│                │               │
   │              │                 │                │               │
   │   REPL >     │                 │                │               │
   │<─────────────│                 │                │               │
   │              │                 │                │               │
```

#### 3.3.2 脚本执行流程

```
┌──────┐      ┌─────────┐      ┌───────┐      ┌──────────┐
│ User │      │ Session │      │ Agent │      │ JS Engine│
└──┬───┘      └────┬────┘      └───┬───┘      └────┬─────┘
   │               │               │               │
   │ load(script)  │               │               │
   │──────────────>│               │               │
   │               │               │               │
   │               │ SCRIPT_LOAD   │               │
   │               │──────────────>│               │
   │               │               │               │
   │               │               │ eval(code)    │
   │               │               │──────────────>│
   │               │               │               │
   │               │               │   execute     │
   │               │               │   bindings    │
   │               │               │<──────────────│
   │               │               │               │
   │               │  SCRIPT_OK    │               │
   │               │<──────────────│               │
   │               │               │               │
   │   loaded      │               │               │
   │<──────────────│               │               │
   │               │               │               │
```

### 3.4 部署架构

```
┌─────────────────────────────────────────────────────────────┐
│                      Development Machine                     │
│  ┌─────────────────────────────────────────────────────┐    │
│  │                    nook-tools/                       │    │
│  │  nook-cli        (可执行文件)                        │    │
│  │  nook-py/        (Python 包)                         │    │
│  │  agent/          (预编译 Agent .so)                  │    │
│  │  server/         (预编译 Server 二进制)              │    │
│  └─────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────┘
                              │
                              │ adb push
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                      Android Device                          │
│                                                              │
│  /data/local/tmp/nook/                                       │
│  ├── nook-server           (设备端守护进程)                  │
│  ├── libnook-agent.so      (注入 Agent)                      │
│  ├── libnook-agent-32.so   (32位 Agent, 可选)                │
│  └── libc++_shared.so      (运行时依赖)                      │
│                                                              │
│  Target App Process                                          │
│  └── libnook-agent.so      (注入后)                          │
│                                                              │
└─────────────────────────────────────────────────────────────┘
```

---

## 4. 模块详细设计

### 4.1 Nook Agent

Agent 是整个框架的核心，运行在目标进程内。

#### 4.1.1 模块结构

```
agent/
├── src/
│   ├── agent_main.cpp           # Agent 入口
│   ├── agent_context.h          # 全局上下文
│   │
│   ├── js_engine/               # JavaScript 引擎封装
│   │   ├── js_runtime.cpp       # QuickJS 运行时管理
│   │   ├── js_runtime.h
│   │   ├── js_value_helper.cpp  # JS 值转换工具
│   │   └── js_value_helper.h
│   │
│   ├── bindings/                # JS Binding 实现
│   │   ├── binding_registry.cpp # Binding 注册中心
│   │   ├── binding_registry.h
│   │   ├── java_binding.cpp     # Java 对象绑定
│   │   ├── java_binding.h
│   │   ├── interceptor_binding.cpp  # Interceptor API
│   │   ├── interceptor_binding.h
│   │   ├── memory_binding.cpp   # Memory API
│   │   ├── memory_binding.h
│   │   ├── module_binding.cpp   # Module API
│   │   ├── module_binding.h
│   │   ├── process_binding.cpp  # Process API
│   │   └── process_binding.h
│   │
│   ├── communication/           # 通信层
│   │   ├── message_handler.cpp  # 消息分发
│   │   ├── message_handler.h
│   │   ├── transport.cpp        # 传输抽象
│   │   └── transport.h
│   │
│   └── core/                    # 现有 Nook 核心 (引用)
│
├── include/
│   └── nook_agent.h             # Agent 公共头文件
│
└── build/
    └── Android.mk
```

#### 4.1.2 Agent 生命周期

```cpp
// agent_main.cpp 伪代码

// Agent 入口点 (通过 dlopen 调用)
__attribute__((constructor))
void nook_agent_init() {
    // 1. 初始化全局上下文
    AgentContext::Initialize();
    
    // 2. 初始化 Nook Core
    NookJavaHookInitialize();
    NookInlineHookInitialize();
    NookPltHookInitialize();
    
    // 3. 初始化 JS 引擎
    JsRuntime::Initialize();
    
    // 4. 注册所有 Binding
    BindingRegistry::RegisterAll();
    
    // 5. 建立与 Server 的通信
    Transport::Connect();
    
    // 6. 通知 Server Agent 就绪
    Transport::SendReady();
    
    // 7. 启动消息循环 (在后台线程)
    MessageHandler::StartLoop();
}

__attribute__((destructor))
void nook_agent_fini() {
    MessageHandler::StopLoop();
    Transport::Disconnect();
    JsRuntime::Shutdown();
    AgentContext::Shutdown();
}
```

#### 4.1.3 JS 引擎封装

```cpp
// js_runtime.h

class JsRuntime {
public:
    static void Initialize();
    static void Shutdown();
    
    // 执行脚本
    static JsResult Evaluate(const std::string& code, const std::string& filename = "<script>");
    
    // 调用 JS 函数
    static JsResult CallFunction(const std::string& name, const std::vector<JsValue>& args);
    
    // 注册全局对象
    static void RegisterGlobal(const std::string& name, const JsObject& obj);
    
    // 获取全局对象
    static JsValue GetGlobal(const std::string& name);
    
private:
    static JSRuntime* runtime_;
    static JSContext* context_;
    static std::mutex mutex_;
};
```

### 4.2 Nook Server

Server 运行在设备端，管理 Agent 注入和通信转发。

#### 4.2.1 模块结构

```
server/
├── src/
│   ├── server_main.cpp          # Server 入口
│   │
│   ├── injector/                # 注入实现
│   │   ├── injector.cpp
│   │   ├── injector.h
│   │   ├── ptrace_injector.cpp  # ptrace 方式
│   │   ├── ptrace_injector.h
│   │   ├── zygote_injector.cpp  # zygote 方式 (可选)
│   │   └── zygote_injector.h
│   │
│   ├── process/                 # 进程管理
│   │   ├── process_manager.cpp
│   │   ├── process_manager.h
│   │   ├── process_info.cpp
│   │   └── process_info.h
│   │
│   ├── session/                 # 会话管理
│   │   ├── session.cpp
│   │   ├── session.h
│   │   ├── session_manager.cpp
│   │   └── session_manager.h
│   │
│   └── transport/               # 通信
│       ├── host_transport.cpp   # 与 Host 通信
│       ├── host_transport.h
│       ├── agent_transport.cpp  # 与 Agent 通信
│       └── agent_transport.h
│
└── build/
    └── Android.mk
```

#### 4.2.2 注入流程

```cpp
// injector.h

class Injector {
public:
    enum class Method {
        PTRACE,     // 通用 ptrace 方式
        ZYGOTE,     // zygote 注入 (需要 root)
        GADGET      // APK 内嵌 (无需注入)
    };
    
    struct Result {
        bool success;
        int error_code;
        std::string error_message;
        uintptr_t agent_base;  // Agent 加载基址
    };
    
    static Result Inject(pid_t pid, const std::string& agent_path, Method method = Method::PTRACE);
    
private:
    static Result InjectViaPtrace(pid_t pid, const std::string& agent_path);
    static Result InjectViaZygote(pid_t pid, const std::string& agent_path);
};
```

#### 4.2.3 Ptrace 注入实现要点

```cpp
// ptrace_injector.cpp 关键流程

Result PtraceInjector::Inject(pid_t pid, const std::string& agent_path) {
    // 1. Attach 到目标进程
    if (ptrace(PTRACE_ATTACH, pid, nullptr, nullptr) < 0) {
        return {false, errno, "ptrace attach failed"};
    }
    waitpid(pid, nullptr, 0);
    
    // 2. 保存原始寄存器
    struct user_regs_struct original_regs, modified_regs;
    ptrace(PTRACE_GETREGS, pid, nullptr, &original_regs);
    modified_regs = original_regs;
    
    // 3. 查找 dlopen 地址
    uintptr_t dlopen_addr = FindRemoteSymbol(pid, "libdl.so", "dlopen");
    
    // 4. 在目标进程写入 agent 路径
    uintptr_t remote_path = WriteRemoteString(pid, agent_path);
    
    // 5. 设置调用参数 (arm64)
    modified_regs.regs[0] = remote_path;           // filename
    modified_regs.regs[1] = RTLD_NOW | RTLD_GLOBAL; // flags
    modified_regs.pc = dlopen_addr;
    modified_regs.sp -= 8;  // 对齐栈
    modified_regs.regs[30] = 0;  // LR = 0 触发 SIGSEGV
    
    // 6. 执行 dlopen
    ptrace(PTRACE_SETREGS, pid, nullptr, &modified_regs);
    ptrace(PTRACE_CONT, pid, nullptr, nullptr);
    
    // 7. 等待调用完成
    waitpid(pid, nullptr, 0);
    
    // 8. 获取返回值 (dlopen 句柄)
    ptrace(PTRACE_GETREGS, pid, nullptr, &modified_regs);
    uintptr_t agent_handle = modified_regs.regs[0];
    
    // 9. 恢复原始状态
    ptrace(PTRACE_SETREGS, pid, nullptr, &original_regs);
    ptrace(PTRACE_DETACH, pid, nullptr, nullptr);
    
    return {agent_handle != 0, 0, "", agent_handle};
}
```

### 4.3 Host 端组件

#### 4.3.1 Python SDK 结构

```
nook-py/
├── nook/
│   ├── __init__.py
│   ├── core.py              # 核心类
│   ├── device.py            # 设备管理
│   ├── session.py           # 会话管理
│   ├── script.py            # 脚本管理
│   ├── rpc.py               # RPC 调用
│   │
│   ├── _transport/          # 通信层
│   │   ├── __init__.py
│   │   ├── adb.py           # ADB 传输
│   │   └── tcp.py           # TCP 传输
│   │
│   └── _proto/              # 协议定义
│       ├── __init__.py
│       └── messages.py
│
├── setup.py
└── requirements.txt
```

#### 4.3.2 Python API 设计

```python
# nook/core.py

import nook

# 获取设备管理器
device_manager = nook.get_device_manager()

# 枚举设备
devices = device_manager.enumerate_devices()

# 获取 USB 设备
device = nook.get_usb_device()

# 枚举进程
processes = device.enumerate_processes()

# Attach 到进程
session = device.attach("com.target.app")
# 或者
session = device.attach(1234)  # by pid

# Spawn 新进程
pid = device.spawn("com.target.app")
session = device.attach(pid)
device.resume(pid)

# 加载脚本
script = session.create_script("""
    Java.perform(function() {
        var Activity = Java.use("android.app.Activity");
        Activity.onCreate.implementation = function(bundle) {
            console.log("Activity.onCreate called!");
            this.onCreate(bundle);
        };
    });
""")

# 设置消息回调
def on_message(message, data):
    print(f"[*] {message}")

script.on("message", on_message)

# 加载并运行
script.load()

# 卸载
script.unload()

# 断开连接
session.detach()
```

#### 4.3.3 CLI 工具设计

```
nook-cli/
├── src/
│   ├── main.cpp
│   ├── commands/
│   │   ├── ps.cpp           # nook-ps
│   │   ├── attach.cpp       # nook -n
│   │   ├── spawn.cpp        # nook -f
│   │   ├── trace.cpp        # nook-trace
│   │   └── discover.cpp     # nook-discover
│   │
│   ├── repl/                # 交互式 REPL
│   │   ├── repl.cpp
│   │   └── completer.cpp
│   │
│   └── output/              # 输出格式化
│       ├── json.cpp
│       └── table.cpp
│
└── build/
    └── CMakeLists.txt
```

**CLI 使用示例**:

```bash
# 列出设备
$ nook-devices
Id    Type    Name
----  ------  ------------------
local usb     Pixel 6

# 列出进程
$ nook-ps -U
 PID  Name
----  --------------------------
1234  com.target.app
5678  com.android.systemui

# Attach 并加载脚本
$ nook -U -n com.target.app -l hook.js

# Spawn 模式
$ nook -U -f com.target.app -l hook.js

# 交互式 REPL
$ nook -U -n com.target.app
     ____  __            __  
    / __ \/ /___  ____  / /__
   / / / / / __ \/ __ \/ //_/
  / /_/ / / /_/ / /_/ / ,<   
 /_____/_/\____/\____/_/|_|  
                             
[USB::Pixel 6::com.target.app]-> Java.enumerateLoadedClasses()
[...]

# 函数追踪
$ nook-trace -U -n com.target.app -i "libc.so!open*"
```

---

## 5. API 设计

### 5.1 JavaScript API

#### 5.1.1 全局对象

```typescript
// 类型定义 (TypeScript 风格)

// 全局 console
declare const console: {
    log(...args: any[]): void;
    warn(...args: any[]): void;
    error(...args: any[]): void;
};

// 发送消息到主机
declare function send(message: any, data?: ArrayBuffer): void;

// 接收主机消息
declare function recv(type: string, callback: (message: any) => void): void;

// 定时器
declare function setTimeout(callback: () => void, delay: number): number;
declare function clearTimeout(id: number): void;
declare function setInterval(callback: () => void, delay: number): number;
declare function clearInterval(id: number): void;

// 十六进制工具
declare function hexdump(target: ArrayBuffer | NativePointer, options?: {
    offset?: number;
    length?: number;
    header?: boolean;
    ansi?: boolean;
}): string;

// 指针创建
declare function ptr(value: string | number): NativePointer;
declare const NULL: NativePointer;
```

#### 5.1.2 Java API

```typescript
declare namespace Java {
    // 执行 Java 操作 (确保在正确线程)
    function perform(fn: () => void): void;
    
    // 延迟执行直到类加载
    function performNow(fn: () => void): void;
    
    // 获取类包装器
    function use<T = any>(className: string): JavaClass<T>;
    
    // 枚举已加载的类
    function enumerateLoadedClasses(callbacks: {
        onMatch: (className: string) => void;
        onComplete: () => void;
    }): void;
    
    // 同步版本
    function enumerateLoadedClassesSync(): string[];
    
    // 枚举类加载器
    function enumerateClassLoaders(callbacks: {
        onMatch: (loader: JavaObject) => void;
        onComplete: () => void;
    }): void;
    
    // 选择堆上的实例
    function choose(className: string, callbacks: {
        onMatch: (instance: JavaObject) => EnumerateAction | void;
        onComplete: () => void;
    }): void;
    
    // 创建数组
    function array(type: string, elements: any[]): JavaArray;
    
    // 注册类
    function registerClass(spec: JavaClassSpec): JavaClass;
    
    // 当前是否在主线程
    function isMainThread(): boolean;
    
    // 调度到主线程
    function scheduleOnMainThread(fn: () => void): void;
    
    // VM 信息
    const vm: {
        getEnv(): any;  // JNIEnv*
        tryGetEnv(): any | null;
    };
}

interface JavaClass<T = any> {
    // 类信息
    $className: string;
    $classLoader: JavaObject | null;
    
    // 创建实例
    $new(...args: any[]): JavaObject<T>;
    
    // 访问静态字段
    [field: string]: any;
    
    // 调用静态方法
    [method: string]: (...args: any[]) => any;
}

interface JavaObject<T = any> {
    $className: string;
    $classLoader: JavaObject | null;
    
    // 访问实例字段
    [field: string]: {
        value: any;
    };
    
    // 调用实例方法
    [method: string]: (...args: any[]) => any;
}

interface JavaMethod {
    // Hook 方法
    implementation: (this: JavaObject, ...args: any[]) => any;
    
    // 调用原始方法
    call(thisObj: JavaObject, ...args: any[]): any;
    
    // 方法重载
    overload(...types: string[]): JavaMethod;
    
    // 获取所有重载
    overloads: JavaMethod[];
    
    // 方法信息
    holder: JavaClass;
    type: string;
    name: string;
    argumentTypes: string[];
    returnType: string;
}
```

**使用示例**:

```javascript
Java.perform(function() {
    // Hook Activity.onCreate
    var Activity = Java.use("android.app.Activity");
    Activity.onCreate.overload("android.os.Bundle").implementation = function(bundle) {
        console.log("Activity.onCreate: " + this);
        
        // 调用原始方法
        this.onCreate(bundle);
    };
    
    // Hook 所有重载
    var String = Java.use("java.lang.String");
    String.valueOf.overloads.forEach(function(overload) {
        overload.implementation = function() {
            var result = overload.apply(this, arguments);
            console.log("String.valueOf => " + result);
            return result;
        };
    });
    
    // 修改返回值
    var SomeClass = Java.use("com.target.SomeClass");
    SomeClass.checkLicense.implementation = function() {
        console.log("checkLicense called, returning true");
        return true;
    };
    
    // 访问字段
    var Config = Java.use("com.target.Config");
    console.log("API_URL = " + Config.API_URL.value);
    Config.DEBUG.value = true;
    
    // 遍历实例
    Java.choose("com.target.UserManager", {
        onMatch: function(instance) {
            console.log("Found UserManager: " + instance.getUsername());
        },
        onComplete: function() {
            console.log("Done");
        }
    });
});
```

#### 5.1.3 Interceptor API (Native Hook)

```typescript
declare namespace Interceptor {
    // 附加到函数
    function attach(target: NativePointer | string, callbacks: InvocationCallbacks): InvocationListener;
    
    // 替换函数
    function replace(target: NativePointer | string, replacement: NativeCallback): void;
    
    // 恢复原始函数
    function revert(target: NativePointer | string): void;
    
    // 刷新指令缓存
    function flush(): void;
}

interface InvocationCallbacks {
    onEnter?: (this: InvocationContext, args: InvocationArguments) => void;
    onLeave?: (this: InvocationContext, retval: InvocationReturnValue) => void;
}

interface InvocationContext {
    // 返回地址
    returnAddress: NativePointer;
    
    // CPU 上下文
    context: CpuContext;
    
    // 线程 ID
    threadId: number;
    
    // 调用深度
    depth: number;
    
    // 自定义数据 (onEnter -> onLeave 传递)
    [key: string]: any;
}

interface InvocationArguments {
    [index: number]: NativePointer;
    length: number;
}

interface InvocationReturnValue {
    replace(value: NativePointer | number): void;
    value: NativePointer;
}

interface InvocationListener {
    detach(): void;
}

declare function setImmediate(callback: (...args: any[]) => void, ...args: any[]): number;
declare function setTimeout(callback: (...args: any[]) => void, delay: number, ...args: any[]): number;
declare function clearTimeout(id: number): void;
declare function setInterval(callback: (...args: any[]) => void, delay: number, ...args: any[]): number;
declare function clearInterval(id: number): void;

// Timer notes:
// - callbacks must be functions
// - setImmediate() and setTimeout(..., 0) are asynchronous
// - clearTimeout() and clearInterval() may cancel either timer type
// - pending timers are cleaned up automatically when a script unloads
```

**使用示例**:

```javascript
// Hook libc open
var openPtr = Module.findExportByName("libc.so", "open");

Interceptor.attach(openPtr, {
    onEnter: function(args) {
        this.path = args[0].readUtf8String();
        console.log("open(" + this.path + ")");
    },
    onLeave: function(retval) {
        console.log("open(" + this.path + ") => " + retval);
    }
});

// 替换函数
var targetPtr = Module.findExportByName("libtarget.so", "verify");

Interceptor.replace(targetPtr, new NativeCallback(function(a, b) {
    console.log("verify called, returning 1");
    return 1;
}, "int", ["pointer", "int"]));

// PLT Hook (通过模块名)
Interceptor.attach("libtarget.so!strcmp", {
    onEnter: function(args) {
        console.log("strcmp: " + args[0].readUtf8String() + " vs " + args[1].readUtf8String());
    }
});
```

#### 5.1.4 Memory API

```typescript
declare namespace Memory {
    // 分配内存
    function alloc(size: number): NativePointer;
    
    // 分配并复制
    function allocUtf8String(str: string): NativePointer;
    function allocAnsiString(str: string): NativePointer;
    function allocUtf16String(str: string): NativePointer;
    
    // 复制内存
    function copy(dst: NativePointer, src: NativePointer, n: number): void;
    
    // 复制数据
    function dup(address: NativePointer, size: number): ArrayBuffer;
    
    // 保护内存
    function protect(address: NativePointer, size: number, protection: string): boolean;
    
    // 查询保护
    function queryProtection(address: NativePointer): Protection;
    
    // 扫描内存
    function scan(address: NativePointer, size: number, pattern: string, callbacks: {
        onMatch: (address: NativePointer, size: number) => void;
        onComplete: () => void;
    }): void;
    
    // 同步扫描
    function scanSync(address: NativePointer, size: number, pattern: string): MemoryScanMatch[];
}

interface Protection {
    readable: boolean;
    writable: boolean;
    executable: boolean;
}

declare class NativePointer {
    constructor(value: string | number);
    
    // 算术运算
    add(offset: number | NativePointer): NativePointer;
    sub(offset: number | NativePointer): NativePointer;
    and(mask: number | NativePointer): NativePointer;
    or(mask: number | NativePointer): NativePointer;
    xor(mask: number | NativePointer): NativePointer;
    
    // 比较
    equals(other: NativePointer): boolean;
    compare(other: NativePointer): number;
    isNull(): boolean;
    
    // 读取
    readS8(): number;
    readU8(): number;
    readS16(): number;
    readU16(): number;
    readS32(): number;
    readU32(): number;
    readS64(): Int64;
    readU64(): UInt64;
    readFloat(): number;
    readDouble(): number;
    readPointer(): NativePointer;
    readByteArray(length: number): ArrayBuffer;
    readUtf8String(size?: number): string;
    readUtf16String(size?: number): string;
    readCString(size?: number): string;
    
    // 写入
    writeS8(value: number): NativePointer;
    writeU8(value: number): NativePointer;
    writeS16(value: number): NativePointer;
    writeU16(value: number): NativePointer;
    writeS32(value: number): NativePointer;
    writeU32(value: number): NativePointer;
    writeS64(value: number | Int64): NativePointer;
    writeU64(value: number | UInt64): NativePointer;
    writeFloat(value: number): NativePointer;
    writeDouble(value: number): NativePointer;
    writePointer(value: NativePointer): NativePointer;
    writeByteArray(value: ArrayBuffer | number[]): NativePointer;
    writeUtf8String(value: string): NativePointer;
    writeUtf16String(value: string): NativePointer;
    
    // 转换
    toString(radix?: number): string;
    toInt32(): number;
    toUInt32(): number;
}
```

**使用示例**:

```javascript
// 读写内存
var addr = ptr("0x12345678");
var value = addr.readU32();
console.log("Value: 0x" + value.toString(16));

addr.writeU32(0xdeadbeef);

// 字符串操作
var strAddr = Memory.allocUtf8String("Hello, Nook!");
console.log(strAddr.readUtf8String());

// 内存搜索
var base = Module.findBaseAddress("libtarget.so");
var size = Module.findRangeByAddress(base).size;

Memory.scan(base, size, "48 89 5c 24 ?? 48 89 6c", {
    onMatch: function(address, size) {
        console.log("Found pattern at: " + address);
    },
    onComplete: function() {
        console.log("Scan complete");
    }
});
```

#### 5.1.5 Module API

```typescript
declare namespace Module {
    // 查找模块
    function findBaseAddress(name: string): NativePointer | null;
    function getBaseAddress(name: string): NativePointer;
    
    // 查找导出
    function findExportByName(moduleName: string | null, exportName: string): NativePointer | null;
    function getExportByName(moduleName: string | null, exportName: string): NativePointer;
    
    // 枚举模块
    function enumerateModules(): Module[];
    
    // 枚举导出
    function enumerateExports(name: string): ModuleExportDetails[];
    
    // 枚举导入
    function enumerateImports(name: string): ModuleImportDetails[];
    
    // 枚举符号
    function enumerateSymbols(name: string): ModuleSymbolDetails[];
    
    // 枚举范围
    function enumerateRanges(name: string, protection: string): RangeDetails[];
    
    // 查找包含地址的范围
    function findRangeByAddress(address: NativePointer): RangeDetails | null;
    
    // 加载模块
    function load(path: string): Module;
}

interface Module {
    name: string;
    base: NativePointer;
    size: number;
    path: string;
}

interface ModuleExportDetails {
    type: "function" | "variable";
    name: string;
    address: NativePointer;
}

interface ModuleImportDetails {
    type: "function" | "variable";
    name: string;
    module: string;
    address: NativePointer;
}

interface RangeDetails {
    base: NativePointer;
    size: number;
    protection: string;
    file?: {
        path: string;
        offset: number;
        size: number;
    };
}
```

**使用示例**:

```javascript
// 列出所有模块
var modules = Module.enumerateModules();
modules.forEach(function(m) {
    console.log(m.name + " @ " + m.base + " size: " + m.size);
});

// 查找函数
var funcAddr = Module.findExportByName("libc.so", "strlen");
console.log("strlen @ " + funcAddr);

// 枚举导出
var exports = Module.enumerateExports("libtarget.so");
exports.forEach(function(exp) {
    if (exp.name.indexOf("verify") !== -1) {
        console.log(exp.name + " @ " + exp.address);
    }
});

// 枚举导入
var imports = Module.enumerateImports("libtarget.so");
imports.filter(function(imp) {
    return imp.module === "libc.so";
}).forEach(function(imp) {
    console.log(imp.name + " from " + imp.module);
});
```

#### 5.1.6 Process API

```typescript
declare namespace Process {
    // 进程 ID
    const id: number;
    
    // 架构
    const arch: "arm" | "arm64" | "x86" | "x64";
    
    // 平台
    const platform: "android" | "linux";
    
    // 页大小
    const pageSize: number;
    
    // 指针大小
    const pointerSize: number;
    
    // 代码签名策略
    const codeSigningPolicy: "optional" | "required";
    
    // 是否被调试
    function isDebuggerAttached(): boolean;
    
    // 当前线程 ID
    function getCurrentThreadId(): number;
    
    // 枚举线程
    function enumerateThreads(): ThreadDetails[];
    
    // 枚举内存范围
    function enumerateRanges(protection: string): RangeDetails[];
    
    // 枚举 malloc 范围
    function enumerateMallocRanges(): RangeDetails[];
    
    // 查找内存范围
    function findRangeByAddress(address: NativePointer): RangeDetails | null;
    
    // 设置异常处理
    function setExceptionHandler(callback: ExceptionHandler): void;
}

interface ThreadDetails {
    id: number;
    state: "running" | "stopped" | "waiting" | "uninterruptible" | "halted";
    context: CpuContext;
}

type ExceptionHandler = (details: ExceptionDetails) => boolean;

interface ExceptionDetails {
    type: string;
    address: NativePointer;
    context: CpuContext;
    nativeContext: NativePointer;
}
```

### 5.2 消息通信 API

```typescript
// Agent -> Host
function send(message: any, data?: ArrayBuffer): void;

// 使用示例
send({ type: "log", payload: "Hello from Agent" });
send({ type: "data" }, new Uint8Array([1, 2, 3, 4]).buffer);

// Host -> Agent (在 recv 中处理)
recv("input", function(message) {
    console.log("Received: " + JSON.stringify(message));
});

// RPC 导出
rpc.exports = {
    add: function(a, b) {
        return a + b;
    },
    
    readMemory: function(address, size) {
        return ptr(address).readByteArray(size);
    },
    
    hookFunction: function(moduleName, funcName) {
        var addr = Module.findExportByName(moduleName, funcName);
        if (addr) {
            Interceptor.attach(addr, {
                onEnter: function(args) {
                    send({ type: "call", func: funcName, args: [args[0].toInt32()] });
                }
            });
            return true;
        }
        return false;
    }
};
```

Python 端调用:

```python
# RPC 调用
result = script.exports.add(1, 2)
print(f"1 + 2 = {result}")

# 读取内存
data = script.exports.read_memory("0x12345678", 16)
print(data.hex())

# 动态 Hook
script.exports.hook_function("libc.so", "open")
```

---

## 6. 通信协议设计

### 6.1 协议概述

采用基于消息的异步双工协议，使用 Protocol Buffers 序列化。

```
┌────────────────────────────────────────────────┐
│                  Message Frame                  │
├──────────┬──────────┬──────────────────────────┤
│  Length  │   Type   │        Payload           │
│  4 bytes │  2 bytes │      Variable            │
└──────────┴──────────┴──────────────────────────┘
```

### 6.2 消息类型

```protobuf
// messages.proto

syntax = "proto3";

package nook.protocol;

// 消息信封
message Envelope {
    uint32 id = 1;           // 消息 ID (用于请求-响应配对)
    MessageType type = 2;    // 消息类型
    bytes payload = 3;       // 序列化的具体消息
}

enum MessageType {
    // 会话管理 (0x01xx)
    ATTACH_REQUEST = 0x0100;
    ATTACH_RESPONSE = 0x0101;
    DETACH_REQUEST = 0x0102;
    DETACH_RESPONSE = 0x0103;
    SPAWN_REQUEST = 0x0104;
    SPAWN_RESPONSE = 0x0105;
    RESUME_REQUEST = 0x0106;
    RESUME_RESPONSE = 0x0107;
    
    // 脚本 (0x02xx)
    SCRIPT_CREATE = 0x0200;
    SCRIPT_LOAD = 0x0201;
    SCRIPT_UNLOAD = 0x0202;
    SCRIPT_RESPONSE = 0x0203;
    
    // 消息传递 (0x03xx)
    SCRIPT_MESSAGE = 0x0300;      // Agent -> Host
    SCRIPT_POST = 0x0301;         // Host -> Agent
    
    // RPC (0x04xx)
    RPC_REQUEST = 0x0400;
    RPC_RESPONSE = 0x0401;
    
    // 进程 (0x05xx)
    PROCESS_LIST_REQUEST = 0x0500;
    PROCESS_LIST_RESPONSE = 0x0501;
    APPLICATION_LIST_REQUEST = 0x0502;
    APPLICATION_LIST_RESPONSE = 0x0503;
    
    // 控制 (0x06xx)
    PING = 0x0600;
    PONG = 0x0601;
    ERROR = 0x06FF;
}

// ============ 会话管理消息 ============

message AttachRequest {
    oneof target {
        uint32 pid = 1;
        string identifier = 2;  // 包名
    }
}

message AttachResponse {
    uint32 session_id = 1;
    uint32 pid = 2;
    string process_name = 3;
    ErrorInfo error = 10;
}

message SpawnRequest {
    string identifier = 1;      // 包名
    repeated string args = 2;   // 启动参数
    map<string, string> env = 3;// 环境变量
}

message SpawnResponse {
    uint32 pid = 1;
    ErrorInfo error = 10;
}

message DetachRequest {
    uint32 session_id = 1;
}

// ============ 脚本消息 ============

message ScriptCreate {
    uint32 session_id = 1;
    string source = 2;          // JS 源代码
    string name = 3;            // 脚本名称
}

message ScriptLoad {
    uint32 script_id = 1;
}

message ScriptUnload {
    uint32 script_id = 1;
}

message ScriptResponse {
    uint32 script_id = 1;
    bool success = 2;
    ErrorInfo error = 10;
}

message ScriptMessage {
    uint32 script_id = 1;
    string message = 2;         // JSON 格式
    bytes data = 3;             // 二进制数据
}

message ScriptPost {
    uint32 script_id = 1;
    string message = 2;         // JSON 格式
    bytes data = 3;
}

// ============ RPC 消息 ============

message RpcRequest {
    uint32 script_id = 1;
    string method = 2;
    repeated bytes args = 3;    // JSON 编码的参数
}

message RpcResponse {
    bytes result = 1;           // JSON 编码的结果
    ErrorInfo error = 10;
}

// ============ 进程消息 ============

message ProcessListRequest {
    // 过滤条件 (可选)
    string name_filter = 1;
}

message ProcessListResponse {
    repeated ProcessInfo processes = 1;
}

message ProcessInfo {
    uint32 pid = 1;
    string name = 2;
    map<string, string> parameters = 3;  // 额外信息
}

message ApplicationListResponse {
    repeated ApplicationInfo applications = 1;
}

message ApplicationInfo {
    string identifier = 1;      // 包名
    string name = 2;            // 显示名
    uint32 pid = 3;             // 运行中的 PID (0 表示未运行)
}

// ============ 错误 ============

message ErrorInfo {
    int32 code = 1;
    string message = 2;
}
```

### 6.3 通信流程示例

#### Attach 流程

```
Host                          Server                        Agent
  │                              │                             │
  │  ATTACH_REQUEST(pid=1234)    │                             │
  │─────────────────────────────>│                             │
  │                              │                             │
  │                              │  inject(pid, agent.so)      │
  │                              │────────────────────────────>│
  │                              │                             │
  │                              │         AGENT_READY         │
  │                              │<────────────────────────────│
  │                              │                             │
  │  ATTACH_RESPONSE(session=1)  │                             │
  │<─────────────────────────────│                             │
  │                              │                             │
```

#### 脚本执行流程

```
Host                          Server                        Agent
  │                              │                             │
  │  SCRIPT_CREATE(source=...)   │                             │
  │─────────────────────────────>│                             │
  │                              │  SCRIPT_CREATE              │
  │                              │────────────────────────────>│
  │                              │                             │
  │                              │         compile JS          │
  │                              │                             │
  │                              │  SCRIPT_RESPONSE(id=1)      │
  │                              │<────────────────────────────│
  │                              │                             │
  │  SCRIPT_RESPONSE(id=1)       │                             │
  │<─────────────────────────────│                             │
  │                              │                             │
  │  SCRIPT_LOAD(id=1)           │                             │
  │─────────────────────────────>│                             │
  │                              │  SCRIPT_LOAD                │
  │                              │────────────────────────────>│
  │                              │                             │
  │                              │         execute JS          │
  │                              │                             │
  │                              │  SCRIPT_MESSAGE(log)        │
  │                              │<────────────────────────────│
  │  SCRIPT_MESSAGE(log)         │                             │
  │<─────────────────────────────│                             │
  │                              │                             │
```

### 6.4 传输层

支持多种传输方式:

| 传输方式 | 使用场景 | 实现 |
|----------|----------|------|
| **ADB Forward** | USB 连接设备 | `adb forward tcp:27042 tcp:27042` |
| **TCP** | 网络连接 (WiFi/模拟器) | 直接 Socket |
| **Unix Socket** | 本地 Server-Agent | `/data/local/tmp/nook/nook.sock` |

```cpp
// transport.h

class Transport {
public:
    virtual ~Transport() = default;
    
    virtual bool Connect() = 0;
    virtual void Disconnect() = 0;
    virtual bool IsConnected() const = 0;
    
    virtual bool Send(const Envelope& envelope) = 0;
    virtual bool Receive(Envelope* envelope, int timeout_ms = -1) = 0;
    
    // 工厂方法
    static std::unique_ptr<Transport> CreateTcp(const std::string& host, int port);
    static std::unique_ptr<Transport> CreateUnixSocket(const std::string& path);
};
```

---

## 7. 技术选型

### 7.1 核心组件选型

| 组件 | 选型 | 备选 | 理由 |
|------|------|------|------|
| **JS 引擎** | QuickJS | Duktape, V8 | 体积小 (~200KB)，ES2020，嵌入友好，性能可接受 |
| **序列化** | Protocol Buffers | FlatBuffers, MessagePack | 跨语言，成熟，代码生成 |
| **Host 语言** | Python 3.8+ | Node.js | Frida 生态兼容，安全社区首选 |
| **CLI 框架** | argparse (Python) | Click | 标准库，无额外依赖 |
| **构建系统** | NDK + CMake | ndk-build | 现代，跨平台，IDE 友好 |

### 7.2 QuickJS 评估

**优点**:
- 极小体积 (~200KB 编译后)
- 完整 ES2020 支持 (async/await, 模块等)
- 无外部依赖，单文件嵌入
- 已在 Frida 部分场景验证
- MIT 许可证

**缺点**:
- 性能不如 V8 (约 2-10x 差距)
- 社区较小
- 调试支持较弱

**结论**: 对于 Hook 框架场景，JS 执行性能非关键路径，QuickJS 的体积优势更重要。

### 7.3 依赖库

| 库 | 版本 | 用途 | 许可证 |
|----|------|------|--------|
| QuickJS | 2024-01-13 | JS 引擎 | MIT |
| protobuf | 3.x | 序列化 | BSD-3 |
| spdlog | 1.x | 日志 | MIT |
| ELFIO | 3.x | ELF 解析 (已有) | MIT |

### 7.4 Android 兼容性

| Android 版本 | API Level | 支持状态 | 备注 |
|--------------|-----------|----------|------|
| 14 | 34 | 计划支持 | 需要适配 |
| 13 | 33 | 主要目标 | |
| 12/12L | 31-32 | 主要目标 | |
| 11 | 30 | 主要目标 | |
| 10 | 29 | 支持 | |
| 9 | 28 | 支持 | |
| 8.x | 26-27 | 最低版本 | |

---

## 8. 实现路线图

### 8.1 Phase 1: 基础框架 (8 周)

**目标**: 建立端到端通信，实现基础 attach/脚本执行

```
Week 1-2: 通信层
├── 定义 Protocol Buffers 消息
├── 实现 Transport 抽象
├── Server 端 TCP/Unix Socket 监听
└── Host 端连接管理

Week 3-4: 注入器
├── Ptrace 注入实现
├── Agent 入口和初始化
├── Server-Agent 本地通信
└── 基础会话管理

Week 5-6: JS 引擎集成
├── QuickJS 编译和嵌入
├── 基础 Binding 框架
├── console.log 实现
├── send/recv 实现
└── 错误处理

Week 7-8: Python SDK v0.1
├── 设备发现
├── attach/detach
├── 脚本加载
├── 消息回调
└── 基础 CLI (nook-ps, nook attach)
```

**交付物**:
- 可以 attach 到进程
- 可以执行简单 JS 脚本
- console.log 输出到主机
- Python SDK 基础功能

### 8.2 Phase 2: Hook 能力 (6 周)

**目标**: 完整的 Java Hook 和 Native Hook API

```
Week 9-10: Java Binding
├── Java.perform 实现
├── Java.use 实现
├── 方法 Hook (implementation)
├── 参数/返回值访问
└── 类枚举

Week 11-12: Interceptor Binding
├── Interceptor.attach (onEnter/onLeave)
├── Interceptor.replace
├── NativePointer 类型
├── 参数读写
└── 返回值修改

Week 13-14: Memory & Module Binding
├── Memory.read*/write*
├── Memory.alloc
├── Module.enumerateModules
├── Module.findExportByName
└── Module.enumerateExports
```

**交付物**:
- 完整 Java Hook API
- 完整 Interceptor API
- Memory/Module API
- 可运行复杂 Hook 脚本

### 8.3 Phase 3: 完善体验 (4 周)

**目标**: 生产可用的工具链

```
Week 15-16: Spawn 模式 & RPC
├── Spawn 实现
├── RPC 调用
├── rpc.exports 支持
└── 热重载支持

Week 17-18: CLI 完善
├── nook-trace 实现
├── REPL 交互模式
├── 输出格式化
├── 错误信息优化
└── 文档编写
```

**交付物**:
- Spawn 模式
- RPC 功能
- 完整 CLI 工具
- 用户文档

### 8.4 Phase 4: 高级功能 (持续)

```
后续迭代:
├── Gadget 模式 (APK 内嵌)
├── arm32 支持
├── 基础 Stalker
├── TypeScript 支持
├── VS Code 扩展
└── 性能优化
```

### 8.5 里程碑总结

| 里程碑 | 时间 | 关键交付物 |
|--------|------|------------|
| **M1** | Week 4 | 端到端通信 + 注入 |
| **M2** | Week 8 | JS 执行 + Python SDK v0.1 |
| **M3** | Week 14 | 完整 Hook API |
| **M4** | Week 18 | 生产可用 v1.0 |

---

## 9. 风险与挑战

### 9.1 技术风险

| 风险 | 影响 | 概率 | 缓解措施 |
|------|------|------|----------|
| **Android 版本碎片** | ART 内部结构变化导致 Java Hook 失败 | 高 | 维护版本适配层，参考 Frida/LSPosed 实现 |
| **SELinux 限制** | 注入被拦截 | 中 | 支持 Magisk 模式，提供 root 方案 |
| **QuickJS 性能** | 复杂脚本执行慢 | 低 | 热路径用 C++ 实现，JS 仅做胶水 |
| **稳定性** | Agent 崩溃导致目标进程崩溃 | 中 | 完善异常处理，隔离脚本执行 |

### 9.2 兼容性挑战

**ART Hook 适配**:
- Android 每个版本的 ART 内部结构都可能变化
- 需要维护 `art_method` 偏移量表
- 参考项目: LSPosed, Pine, SandHook

**Linker 变化**:
- Android 7.0+ 的 namespace 隔离
- Android 10+ 的 linker 变化
- 需要适配不同版本的 `dlopen` 行为

### 9.3 应对策略

1. **版本检测机制**: 运行时检测 Android 版本和 ART 版本
2. **动态偏移量**: 通过符号或模式匹配获取结构偏移
3. **降级方案**: 某些功能在旧版本不可用时提供替代方案
4. **社区参考**: 密切关注 Frida、LSPosed 等项目的适配方案

---

## 10. 附录

### 10.1 参考项目

| 项目 | 参考点 |
|------|--------|
| [Frida](https://frida.re) | API 设计, 架构 |
| [LSPosed](https://github.com/LSPosed/LSPosed) | ART Hook 实现 |
| [SandHook](https://github.com/asLody/SandHook) | Java Hook |
| [Dobby](https://github.com/jmpews/Dobby) | Inline Hook |
| [xHook](https://github.com/nicklin/xhook) | PLT Hook |
| [QuickJS](https://bellard.org/quickjs/) | JS 引擎 |

### 10.2 目录结构 (最终)

```
nook-framework/
├── agent/                      # Device 端 Agent
│   ├── src/
│   │   ├── agent_main.cpp
│   │   ├── js_engine/
│   │   ├── bindings/
│   │   └── communication/
│   ├── include/
│   └── build/
│
├── server/                     # Device 端 Server
│   ├── src/
│   │   ├── server_main.cpp
│   │   ├── injector/
│   │   ├── session/
│   │   └── transport/
│   └── build/
│
├── core/                       # 现有 Nook 核心 (重组)
│   ├── include/nook/
│   ├── src/
│   │   ├── java_hook/
│   │   ├── native_hook/
│   │   └── common/
│   └── build/
│
├── protocol/                   # 协议定义
│   └── messages.proto
│
├── host/                       # Host 端
│   ├── nook-py/               # Python SDK
│   │   ├── nook/
│   │   ├── setup.py
│   │   └── requirements.txt
│   │
│   └── nook-cli/              # CLI 工具
│       └── src/
│
├── third_party/               # 第三方依赖
│   ├── quickjs/
│   ├── protobuf/
│   └── elfio/
│
├── examples/                  # 示例脚本
│   ├── java_hook.js
│   ├── native_hook.js
│   └── memory_dump.js
│
├── docs/                      # 文档
│   ├── api/
│   ├── guides/
│   └── internals/
│
└── tools/                     # 构建工具
    ├── build_agent.sh
    ├── build_server.sh
    └── package.sh
```

### 10.3 版本规划

| 版本 | 预计时间 | 主要特性 |
|------|----------|----------|
| v0.1.0 | M2 | 基础 attach + 脚本执行 |
| v0.5.0 | M3 | 完整 Hook API |
| v1.0.0 | M4 | 生产可用, 文档完善 |
| v1.1.0 | M4+4w | Gadget 模式 |
| v1.2.0 | M4+8w | arm32 支持 |
| v2.0.0 | 未定 | Stalker, iOS |

---

## 文档修订历史

| 版本 | 日期 | 作者 | 变更说明 |
|------|------|------|----------|
| 1.0 | 2026-04-22 | - | 初始版本 |

---

*End of Document*
