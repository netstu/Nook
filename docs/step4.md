• 我先把现状看清了：现在 spawn/attach/call/post/unload 都是“一次命令，一次连接，一次脚本生命周期”，所以已经能用，但还不
  够像 Frida 的核心原因，是没有持久会话。

  先确认一个关键目标，我再给你 2 到 3 个方案做取舍：

  你现在要的“像 Frida”，优先是 A 这种单个 nook-cli 进程内长期保持会话的 repl/interactive shell，还是 B 这种跨多次 CLI 命
  令也能复用同一个 session/script 的“后台持久会话”？


› A


• 我按 A 收敛，目标就是单个 nook-cli 进程内保持 Device + Session + Script 常驻，后续在同一个交互 shell 里反复 post/call/
  unload/exit，不再每次重走 spawn/attach + load。

• 那就走 A。我建议的 3 个方案如下。

  1. 最小扩展：在现有 spawn/attach --interactive 上继续堆命令解析
     优点：改动最小，复用现有 stdin -> script.post() 线程。
     缺点：现在的 interactive 本质只是“把每行当 post 发出去”，要再塞 call/unload/reload/help/exit 会越来越别扭，代码也会
     脏。
  2. 新增独立 repl/shell 模式
     优点：结构最清晰，像 Frida；进入后会话常驻，可以支持：
     %post {...}
     %call ping ["x"]
     %load hook.js
     %unload
     %resume
     %help
     %exit
     普通输入也可以约定成默认 post。
     缺点：CLI 要新开一套子命令和事件循环，改动中等。
  3. 做 host 后台 daemon，再由前台 CLI 连过去
     优点：以后可以演进成跨命令复用会话。
     缺点：明显超出你现在选的 A，复杂度高，先不该做。

  我的建议是 2。原因很直接：它能把“持久会话”单独建模，不会污染现有 spawn/attach/call/post 一次性命令，后面再扩展也最顺。

  设计第一段先给你看：命令形态。

  我建议新增：
  - nook-cli repl attach com.demo.target -l hook.js --usb

  进入后支持：

  - %post {"type":"post","payload":"hello"}
  - %call ping ["hello"]
  - %load E:\path\hook.js
  - %unload
  - %resume
  - %help
  - %exit

  约定：

  - 非 % 开头的输入，默认按 %post <整行文本> 处理
  - 后台持续打印 script message
  - 当前会话里只维护一个“活动 script”，先不做多 script 管理

  这段如果你认可，我下一条就继续给你第二段设计：内部结构怎么拆，尽量少改现有代码。


› 可以


• 命令形态定了，我继续把内部结构和边界条件收束出来，保证这一步是可落地的最小实现，不会把现有 CLI 搅乱。

• 第二段设计是内部结构，我也建议尽量克制，只补一层 host 侧会话壳，不动协议。

  核心结构：

  - 新增 nook-cli repl ...
  - CLI 建一个 ReplContext
  - ReplContext 持有：
      - device
      - session
      - script
      - script_id
      - 当前连接模式信息 spawn/attach
      - 当前脚本路径和脚本名
  - 进入 REPL 后主线程只做两件事：
      - 读用户输入并执行本地命令
      - 持续消费 device.wait_for_script_message() 并打印异步消息

  实现方式我建议是双线程：

  - 输入线程：阻塞读 stdin，解析 %call/%post/%load/%unload/%exit
  - 消息线程：循环 wait_for_script_message(timeout_ms=...)
    这样和你现在 --wait 的行为一致，心智模型最简单。

  命令语义：

  - %load <path>：读文件，create + load，替换当前活动 script
  - %unload：卸载当前 script，并清空活动 script 引用
  - %post <json-or-text>：发给当前 script
  - %call <method> <json-array>：调用当前 script 的 rpc.exports
  - %help：打印命令帮助

  边界约束：

  - 没有活动 script 时，%post/%call/%unload 直接报错
  - %load 默认不自动 unload 旧 script；我建议实现成：
      - 如果已有活动 script，先自动 unload
      - 再加载新 script
        这样更接近“替换当前脚本”
  - 先不做多 script 列表，也不做 script rename
  - 先不做 attach session 的持久 detach 管理，退出时直接断 host 连接

  我推荐这个设计，因为它完全复用现有 Device/Session/Script，新增的只是：

  - CLI parser 的 repl 子命令
  - 一个 REPL 循环
  - 少量辅助函数

  第三段设计是测试和验收口径，如果你也同意，我就写设计文档并进入实现。


› 可以


• 我把最后一段收口成测试和验收边界，确认后我就把设计落到 docs/plans，再切到实现计划。

• 第三段是测试与验收，我建议只做一套“最小可证明可用”的闭环，不额外扩功能。

  验收范围：

  - repl spawn <pkg> -l hook.js --resume --usb 能进入交互态
  - repl attach <pkg> -l hook.js --usb 能进入交互态
  - 进入后异步 script message 能持续打印
  - %post {"type":"post","payload":"x"} 能收到脚本回包
  - %call ping ["hello"] 能拿到 RPC 返回
  - %unload 后再 %call/%post 会报“当前无活动 script”
  - %load <path> 能重新加载脚本并恢复可用
  - %help 和 %exit 行为正常

  代码测试我建议拆两层：

  - Python 单测：
      - parser 能识别 repl spawn/repl attach
      - REPL 命令分发正确
      - %load/%unload/%post/%call 在 FakeDevice/FakeScript 上行为正确
      - 无活动 script 的错误路径覆盖
  - 真机冒烟：
      - 用现有 hook.js
      - 验证 %post
      - 验证 %call
      - 验证 %unload -> %load -> %call

  明确不做的内容：

  - 不做跨多个 CLI 进程复用 session
  - 不做多 script 管理
  - 不做 detach/recover/reconnect
  - 不做 Promise/异步 RPC
  - 不做完整 Frida 命令兼容

  如果这段也没问题，我下一步就：

  1. 写设计文档到 docs/plans/2026-04-23-persistent-repl-design.md
  2. 切到 writing-plans，给出实现计划，然后开始改代码。

---

## Review 建议 (2026-04-23)

方案整体很好，选择方案 2（独立 REPL 模式）是正确的。以下是一些补充建议：

### 1. 建议增加的命令

| 命令 | 说明 |
|------|------|
| `%info` | 显示当前状态：pid、process_name、script_id、脚本路径 |
| `%reload` | 等价于 `%unload` + `%load <上次路径>`，快速重载当前脚本 |

`%reload` 在调试迭代时非常有用，不用每次输入完整路径。

### 2. Prompt 格式建议

让用户知道当前状态：

```
# 无脚本时
[com.demo.target:4321] > 

# 有脚本时
[com.demo.target:4321 hook.js] > 

# spawn 未 resume 时
[com.demo.target:4321 SUSPENDED] > 
```

### 3. readline 支持

Python 的 `readline` 模块可以提供：
- 上下键历史
- 行编辑 (Ctrl+A/E/K/U)
- 可选的命令补全

```python
import readline
readline.parse_and_bind('tab: complete')
# 历史自动保存到 ~/.nook_history
```

这个是可选增强，不影响核心功能，可以后做。

### 4. 线程退出处理

消息线程需要能优雅退出：

```python
class ReplContext:
    def __init__(self):
        self._stop_event = threading.Event()
    
    def _message_loop(self):
        while not self._stop_event.is_set():
            try:
                message = self.device.wait_for_script_message(timeout_ms=500)
                self._print_message(message)
            except TimeoutError:
                continue
    
    def stop(self):
        self._stop_event.set()
```

### 5. RPC 超时处理

`%call` 应该有合理的超时提示：

```
[com.demo.target:4321 hook.js] > %call slowMethod []
error: rpc call timed out (5000ms)
```

### 6. spawn 后 resume 时机

两种模式都应支持：

```bash
# 模式 1: 启动时 resume
nook-cli repl spawn com.app -l hook.js --resume --usb

# 模式 2: 进入 REPL 后手动 resume
nook-cli repl spawn com.app -l hook.js --usb
[com.app:4321 SUSPENDED hook.js] > %resume
resume ok
[com.app:4321 hook.js] > 
```

设计文档已经包含 `%resume`，确认这个场景可覆盖。

### 7. 输入解析边界

明确几个边界：

```
# 空行 -> 忽略
# 纯空白 -> 忽略
# %unknown -> "unknown command, type %help"
# %call (无参数) -> "usage: %call <method> [args_json]"
# %post (无参数) -> "usage: %post <message>"
```

### 8. JSON 解析容错

`%call` 的参数可以考虑：

```
%call ping ["hello"]     # 标准 JSON 数组
%call ping "hello"       # 单参数简写 -> ["hello"]
%call ping               # 无参数 -> []
```

这个是可选的 UX 优化，不做也可以。

---

### 总结

设计方案合理，边界清晰，可以开始实现。建议实现顺序：

1. 先做最小 REPL 骨架：`%post` / `%exit` / 消息打印
2. 再加 `%load` / `%unload`
3. 再加 `%call`
4. 最后加 `%info` / `%reload` / `%help` / prompt 美化

这样可以逐步验证，每一步都有可测试的产出。