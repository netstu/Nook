# REPL 实现 Review (2026-04-22)

## 总体评价

REPL 实现完成度很高，完全覆盖了 step4.md 设计文档中定义的所有功能。代码结构清晰，关键边界条件都有处理，测试覆盖全面。

### 亮点

1. **设计落地准确**
   - `repl spawn` / `repl attach` 子命令结构清晰
   - 所有 `%` 命令都已实现：`%post`, `%call`, `%load`, `%unload`, `%resume`, `%info`, `%reload`, `%help`, `%exit`
   - 非 `%` 开头的输入正确当作 `%post` 处理

2. **deferred script loading 处理得当**
   - spawn 模式下如果没有 `--resume`，脚本路径会保存但不立即加载
   - `%resume` 时自动加载之前指定的脚本
   - 这是解决 SIGSTOP 阻塞 agent recv 线程问题的正确方案

3. **状态管理简洁**
   - 使用 `SimpleNamespace` 管理 REPL 上下文，字段清晰：
     - `device`, `session`, `script`, `script_id`
     - `process_name`, `pid`, `suspended`
     - `script_path`, `script_name`, `pending_script_path`
   - prompt 格式化能反映当前状态（进程名、pid、脚本名、SUSPENDED）

4. **错误边界覆盖**
   - 无活动脚本时 `%post/%call/%unload/%reload` 报错
   - attach 模式下 `%resume` 报错（只有 spawn 才能 resume）
   - `%call` 参数 JSON 解析失败有明确提示
   - `%load` 文件不存在有明确提示

5. **测试覆盖全面**
   - parser 测试：验证 `repl spawn`、`repl attach` 解析
   - prompt 测试：各种状态组合的 prompt 格式
   - 命令解析测试：各种 `%` 命令和普通文本
   - REPL 流程测试：spawn/attach 完整流程、deferred load、错误路径
   - FakeDevice/FakeSession/FakeScript mock 设计合理

---

## 具体代码分析

### cli.py 核心函数

| 函数 | 行号 | 说明 |
|------|------|------|
| `_format_repl_prompt()` | 183-189 | 格式化 prompt，包含进程名、pid、SUSPENDED 状态、脚本名 |
| `_parse_repl_command()` | 192-214 | 解析用户输入，支持 %command 和普通文本 |
| `_handle_repl_command()` | 327-401 | 分发执行各个 % 命令 |
| `_run_repl()` | 404-427 | 主循环，消息线程 + 输入循环 |
| `_create_repl_context()` | 430-479 | 根据 spawn/attach 模式创建上下文 |
| `_repl_defer_script_load()` | 280-283 | 延迟脚本加载，用于 suspended spawn |

### 设计一致性

step4.md 定义的功能对照：

| 设计要求 | 实现状态 |
|----------|----------|
| `%post <json-or-text>` | 已实现 |
| `%call <method> [args]` | 已实现 |
| `%load <path>` | 已实现，自动 unload 旧脚本 |
| `%unload` | 已实现 |
| `%resume` | 已实现，spawn 模式专用 |
| `%info` | 已实现，显示 pid/process/script |
| `%reload` | 已实现，unload + load 上次路径 |
| `%help` | 已实现 |
| `%exit` | 已实现，正确调用 device.close() |
| 普通输入当 post | 已实现 |
| 动态 prompt | 已实现 |
| 消息线程持续打印 | 已实现 |
| 无脚本时报错 | 已实现 |

---

## 改进建议

### 1. 消息线程优雅退出

当前 `_run_repl()` 中消息线程使用 daemon=True，进程退出时线程直接终止。建议增加 `_stop_event` 来优雅停止：

```python
# 在 ReplContext 中增加
ctx.stop_event = threading.Event()

# 消息线程循环
while not ctx.stop_event.is_set():
    try:
        msg = ctx.device.wait_for_script_message(timeout_ms=500, script_id=ctx.script_id)
        _print_script_message(msg)
    except TimeoutError:
        continue

# %exit 时
ctx.stop_event.set()
```

### 2. RPC 超时提示

`%call` 应该在超时时给出更友好的提示：

```python
except TimeoutError:
    print(f"error: rpc call timed out ({timeout_ms}ms)")
```

### 3. readline 支持（可选增强）

Python 的 `readline` 模块可以提供命令历史和行编辑：

```python
try:
    import readline
    readline.parse_and_bind('tab: complete')
except ImportError:
    pass  # Windows 下可能不可用
```

### 4. %call 参数简化（可选 UX 优化）

```python
# 当前必须写完整 JSON
%call ping ["hello"]

# 建议支持简写
%call ping hello      # -> ["hello"]
%call ping            # -> []
```

---

## 下一步计划

### 1. 真机端到端测试（优先级：高）

当前测试都是基于 FakeDevice mock，需要在真机上验证完整流程：

```bash
# 测试 spawn + REPL
adb forward tcp:27042 tcp:27042
nook-cli repl spawn com.demo.target -l hook.js --usb

# 在 REPL 中测试
%info
%post {"type":"ping"}
%call ping ["hello"]
%unload
%load hook.js
%reload
%resume
%exit
```

验收点：
- [ ] spawn 后进程正确挂起
- [ ] %resume 后进程正常运行
- [ ] 脚本消息能持续打印
- [ ] %post 消息能到达 agent
- [ ] %call 能调用 rpc.exports 并返回结果
- [ ] %unload/%load/%reload 能正确切换脚本

### 2. Agent 端 RPC 实现（优先级：高）

当前 Agent 侧还没有实现 rpc.exports 注册和调用机制：

```cpp
// Agent 端需要实现
class RpcRegistry {
public:
    void RegisterMethod(const std::string& name, RpcHandler handler);
    std::string Call(const std::string& method, const std::string& args_json);
};
```

### 3. QuickJS 集成（优先级：中）

要支持真正的 JavaScript 脚本，需要在 Agent 端集成 QuickJS：

- 编译 QuickJS 为 Android 静态库
- 实现 JS 运行时包装
- 实现 send/recv/rpc.exports 的 JS binding
- 实现脚本热加载（load/unload/reload）

### 4. Server 持久化（优先级：低）

当前每次 CLI 命令都是独立连接。如果需要跨命令复用 session，需要实现后台 daemon：

```
nook-server start    # 启动后台服务
nook-cli attach ...  # 连接到已有服务
nook-server stop     # 停止服务
```

这是 step4.md 中提到的方案 3，当前阶段不需要做。

---

## 总结

REPL 实现质量很高，设计文档中的所有功能都已落地，测试覆盖全面。代码结构清晰，边界条件处理到位。

**当前完成度**: Host 侧 Python SDK + CLI 基本完成，可以进入真机测试阶段。

**下一个里程碑**: 在真机上跑通 spawn -> load -> post/call -> unload -> exit 的完整流程，验证 Host-Server-Agent 三层通信。
