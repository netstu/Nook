# Nook 通信层 Code Review

> 审核日期: 2026-04-22

---

## 1. 已完成模块

### 1.1 传输层 (Transport)
| 文件 | 状态 | 说明 |
|------|------|------|
| `transport.h/cpp` | ✅ | 抽象基类，SendAll/RecvAll 实现 |
| `tcp_transport.h/cpp` | ✅ | TCP 客户端/服务端，支持超时 |
| `unix_transport.h/cpp` | ✅ | Unix Socket，支持 peer credentials |

### 1.2 协议层 (Protocol)
| 文件 | 状态 | 说明 |
|------|------|------|
| `message_types.h` | ✅ | 完整的消息类型枚举 |
| `frame.h/cpp` | ✅ | 10 字节帧头，粘包处理 |
| `tlv.h/cpp` | ✅ | TLV 编解码器 |
| `messages.h/cpp` | ✅ | 所有消息结构及编解码函数 |

### 1.3 会话层 (Session)
| 文件 | 状态 | 说明 |
|------|------|------|
| `session.h/cpp` | ✅ | 接收线程，请求-响应关联 |
| `session_manager.h/cpp` | ✅ | 多会话管理 |

### 1.4 I/O 事件循环
| 文件 | 状态 | 说明 |
|------|------|------|
| `io_loop.h/cpp` | ✅ | epoll 事件循环 |

### 1.5 Server 端
| 文件 | 状态 | 说明 |
|------|------|------|
| `server_main.cpp` | ✅ | Server 入口，TCP/Unix 监听 |
| `server_handlers.cpp` | ✅ | 完整的消息处理器 |
| `session_registry.cpp` | ✅ | Host/Agent 会话注册与绑定 |
| `ninjector_spawn_injector.cpp` | ✅ | Ninjector spawn 注入 |
| `process_manager.cpp` | ✅ | 进程枚举 |

### 1.6 Agent 端
| 文件 | 状态 | 说明 |
|------|------|------|
| `NookComm.cpp` | ✅ | Agent 通信 API，已移除内部 spawn gate |
| `agent_connection.cpp` | ✅ | Agent 会话管理 |

### 1.7 Host 端
| 文件 | 状态 | 说明 |
|------|------|------|
| `host_spawn_client.cpp` | ✅ | Spawn/Script 操作封装 |

---

## 2. Spawn 模型 Review

### 2.1 原问题回顾

之前的实现采用 "agent 内部 gate 等待" 模型：

```
Agent constructor → NookCommWaitForResumeIfSpawned() → 阻塞等待 → 💥 死锁
```

问题：Agent 在 payload constructor 里直接调用 gate 等待，但此时 Server 还没完成 spawn callback 绑定。

### 2.2 当前实现 (Frida 模型)

改为 **外部暂停 + SIGCONT 恢复**：

```
Server:
  1. Ninjector.Spawn() → 进程启动 + agent 注入
  2. SIGSTOP(pid) → 进程外部暂停
  3. SpawnResponse 返回给 Host
  4. Host 调用 Resume → SIGCONT(pid) → 进程恢复
```

**关键代码位置**：

- `server_handlers.cpp:169-178` - Spawn 后立即 SIGSTOP
- `server_handlers.cpp:374` - Resume 时 SIGCONT
- `NookComm.cpp:195-201` - Agent 侧 `WaitForResumeIfSpawned()` 直接返回 OK

### 2.3 时序确认

```
Host                    Server                      Target Process
  │                        │                              │
  │  SPAWN_REQUEST         │                              │
  │───────────────────────>│                              │
  │                        │  Ninjector.Spawn()           │
  │                        │─────────────────────────────>│
  │                        │                              │ (agent 加载)
  │                        │  SIGSTOP                     │
  │                        │─────────────────────────────>│ (暂停)
  │                        │                              │
  │  SPAWN_RESPONSE        │                              │
  │<───────────────────────│                              │
  │                        │  Agent→Server AGENT_READY    │
  │                        │<─────────────────────────────│
  │  AGENT_READY (forward) │                              │
  │<───────────────────────│                              │
  │                        │                              │
  │  (Host 加载脚本)        │                              │
  │                        │                              │
  │  RESUME_REQUEST        │                              │
  │───────────────────────>│                              │
  │                        │  SIGCONT                     │
  │                        │─────────────────────────────>│ (恢复运行)
  │  RESUME_RESPONSE       │                              │
  │<───────────────────────│                              │
```

**结论**：Spawn 模型正确，无竞态条件。

---

## 3. 待改进项

### 3.1 HostSpawnClient 缺少 Resume 方法

当前 `host_spawn_client.h` 没有 `Resume()` 方法，Host 无法发送 RESUME_REQUEST。

**建议**：

```cpp
// host_spawn_client.h
bool Resume(uint32_t pid, 
            int timeout_ms,
            ResumeResponse* response,
            std::string* error_message = nullptr);
```

### 3.2 AgentSpawnSuspended 消息未使用

`messages.h:55-57` 定义了 `AgentSpawnSuspended`，但当前 spawn 模型不再需要它（Agent 不再主动上报暂停状态）。

**建议**：保留但标记为 deprecated，或直接移除。

### 3.3 Session RecvLoop 异常处理

`session.cpp` 的 `RecvLoop` 无法区分：
- 超时（无数据）
- 连接断开
- 解析错误

**建议**：Transport::Recv 返回值区分这三种情况。

### 3.4 缺少 Payload 大小限制

Frame 解析没有限制 payload 大小，恶意客户端可发送超大帧导致 OOM。

**建议**：添加 `kMaxPayloadSize` 常量（如 16MB），解析时校验。

---

## 4. 下一步计划

### Phase 8: Python SDK (优先级高)

实现 Host 端 Python 客户端库，提供类 Frida 的 API 体验。

**目录结构**：
```
host/nook-py/
├── nook/
│   ├── __init__.py
│   ├── core.py          # 主入口 (get_device, etc.)
│   ├── device.py        # Device 类
│   ├── session.py       # Session 类
│   ├── script.py        # Script 类
│   └── _transport.py    # TCP 通信封装
├── setup.py
└── pyproject.toml
```

**使用示例**：
```python
import nook

device = nook.get_usb_device()
session = device.spawn("com.target.app")

script = session.create_script("""
    Java.perform(function() {
        var Activity = Java.use("android.app.Activity");
        Activity.onCreate.implementation = function(bundle) {
            send({type: "log", message: "onCreate!"});
            this.onCreate(bundle);
        };
    });
""")
script.on("message", lambda msg, data: print(msg))
script.load()

device.resume(session.pid)
input("Press Enter to exit...")
```

**关键任务**：
1. 实现 TLV/Frame 编解码 (Python)
2. TCP 连接封装 + adb forward
3. Device/Session/Script 类
4. 消息回调机制

### Phase 9: QuickJS 集成 (优先级高)

在 Agent 侧集成 QuickJS 引擎，实现动态脚本执行。

**模块划分**：
```
src/scripting/
├── js_runtime.h         # QuickJS 封装
├── js_runtime.cpp
├── js_bindings.h        # Nook API 绑定
├── js_bindings.cpp
└── script_manager.h     # 脚本生命周期管理
```

**绑定 API 设计**：
```javascript
// console
console.log("message");

// 消息发送
send({type: "log", payload: "hello"});

// 接收回调
recv(function(message) {
    console.log("received: " + JSON.stringify(message));
});

// Java Hook (后续)
Java.perform(function() {
    var Activity = Java.use("android.app.Activity");
    Activity.onCreate.implementation = function(bundle) {
        console.log("onCreate called");
        this.onCreate(bundle);
    };
});
```

**关键任务**：
1. 集成 QuickJS (third_party/quickjs)
2. 实现 `console`, `send`, `recv` 绑定
3. ScriptCreate/Load/Unload 处理器接入 QuickJS
4. 错误捕获和上报

### Phase 10: 端到端测试

**测试场景**：
1. Spawn + Script 加载 + Hook 验证
2. Attach + 热加载脚本
3. 断线重连
4. 多 Session 并发
5. 大脚本传输

### Phase 11: CLI 工具

```
nook-cli spawn com.target.app -l hook.js
nook-cli attach 12345 -l hook.js  
nook-cli ps
nook-cli apps
```

---

## 5. 优先级排序

| 序号 | 任务 | 优先级 | 依赖 |
|------|------|--------|------|
| 1 | HostSpawnClient.Resume() | 高 | - |
| 2 | Python SDK 基础框架 | 高 | 1 |
| 3 | QuickJS 集成 | 高 | - |
| 4 | Python SDK Script 支持 | 中 | 2, 3 |
| 5 | Session 异常处理优化 | 中 | - |
| 6 | Payload 大小限制 | 中 | - |
| 7 | CLI 工具 | 低 | 2 |
| 8 | 端到端测试 | 低 | 2, 3, 4 |

---

## 6. 附录：模块依赖图

```
                    ┌──────────────┐
                    │  Python SDK  │
                    └──────┬───────┘
                           │ TCP
                           ▼
┌──────────────────────────────────────────────────────────────┐
│                      Nook Server                             │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────┐  │
│  │ TCP Listener│  │SessionRegistry│ │ MessageDispatcher │  │
│  └─────────────┘  └─────────────┘  └─────────────────────┘  │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────┐  │
│  │Unix Listener│  │   Injector  │  │  ProcessManager     │  │
│  └─────────────┘  └─────────────┘  └─────────────────────┘  │
└──────────────────────────────────────────────────────────────┘
           │ Unix Socket
           ▼
┌──────────────────────────────────────────────────────────────┐
│                      Target Process                          │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────┐  │
│  │ NookComm    │  │AgentConnection│ │   QuickJS (TODO)  │  │
│  └─────────────┘  └─────────────┘  └─────────────────────┘  │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────┐  │
│  │ Java Hook   │  │  PLT Hook   │  │   Inline Hook       │  │
│  └─────────────┘  └─────────────┘  └─────────────────────┘  │
└──────────────────────────────────────────────────────────────┘
```
