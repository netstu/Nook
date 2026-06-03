# Nook 开发历程整理

## 1. 最开始：先把三层通信骨架搭起来

项目最早不是从“Hook 很强”开始的，而是先把 Frida 类似的三层结构搭出来：

- Host
- Device Server
- Target Agent

这一阶段的核心是通信层，不是 Hook 层。先完成了：

- transport：TCP / Unix socket
- protocol：frame / tlv / message types
- session：请求响应关联、接收线程
- server 侧：session registry、message dispatcher、spawn injector、process manager
- agent 侧：`NookComm`、agent connection
- host 侧：最早的 spawn client

这一阶段的关键收获是：

- 项目从一开始就不是“单库 Hook”，而是按 Frida 的 Host-Server-Agent 分层方向走的
- 后续所有能力，都是在这个控制平面上往上长出来的

## 2. 第一轮关键转折：spawn 语义从 agent 内部门控改成 server 外部挂起

最开始 spawn 方案是 agent 自己在 constructor 里等待 resume，这条路本质上有时序死锁风险。

后来改成更接近 Frida 的方式：

- server 发起 spawn
- target 启动并注入 agent
- server 侧 `SIGSTOP`
- host 拿到 spawn response 后再决定何时 `resume`
- resume 时 `SIGCONT`

这是一个很关键的设计转折，因为它把“进程挂起 / 恢复”的控制权放回了 server，而不是放在 payload 初始化流程里。之后很多问题，实际上都是围绕这条 spawn 语义继续打磨。

## 3. Host 能力开始成形：Python SDK、CLI、REPL

通信骨架和基本 spawn 语义稳定后，开始补 Host 体验：

- Python SDK
- CLI
- REPL

这一阶段不是简单“写个命令行”，而是在模仿 Frida 的交互模式：

- `spawn`
- `attach`
- `load`
- `post`
- `call`
- `unload`
- `resume`
- `repl`

REPL 后来做成了独立工作流，并补齐了：

- `%post`
- `%call`
- `%load`
- `%unload`
- `%resume`
- `%info`
- `%reload`
- `%help`
- `%exit`

这里的一个重要转折是：项目开始从“能调用底层协议”变成“能让人像用 Frida 一样工作”。

## 4. Native runtime 与 JS bridge 打通：QuickJS + Frida 风格基础 API

后面进入真正的 Agent runtime 阶段：

- QuickJS 集成
- `Module` / `Process` / `Memory`
- `NativePointer`
- `NativeFunction`
- `NativeCallback`
- `Interceptor.attach`
- `replace / revert`
- `Thread.backtrace`
- `DebugSymbol`

这一阶段的本质不是“补 API 名字”，而是建立一套 Frida 风格的 JS runtime 语义，让脚本可以真实驱动 native hook。

之后又补了延迟安装能力：

- 模块未加载时先 accept 脚本
- 模块出现后再自动 install hook

这是 Nook 从“能 Hook 一些固定地址”进化到“更接近真实逆向工作流”的关键一步。

## 5. 一个很大的阶段性结论：Native 侧先接近可用，差距逐渐收敛到 Java 和易用性

到了这个阶段，项目已经发现：

- Native 基础设施已经比较完整
- 真正的差距开始转移到 Java Hook、边角语义、稳定性和工具体验

也就是说，问题不再是“能不能做”，而开始变成：

- 稳不稳定
- 语义像不像 Frida
- 热路径性能够不够
- 用户脚本能不能无痛迁移

这是项目从“原型”进入“工具化”阶段的分界点。

## 6. Java 路线开始打通：`Java.perform` 到常用 API 子集

随后进入 Java 侧补齐阶段。先做的是最小链路：

- `Java.perform`
- `Java.use`
- `.implementation = fn`
- `callOriginal`

后面又逐步补了：

- `Java.choose`
- `cast`
- `retain`
- `field`
- `overload`
- loader / `ClassFactory`
- `Java.openClassFile`
- `Java.registerClass`
- `Java.array`

这部分不是一次性顺利完成的，中间是靠大量真机 case 往前拱出来的。你后面用文章里的那些 demo 和 Frida Labs 去打，实际上就是在拿真实脚本逼 Nook 的 Java 语义往 Frida 靠。

## 7. Java 阶段最大的特点：不是纯功能缺失，而是不断暴露语义和架构边界

这一阶段出现了很多典型问题，后面都沉淀成 bug 文档或行为边界：

- `.overload(...)` 某些场景崩溃
- 构造函数 hook 一开始不稳定
- `choose` 相关场景里，私有字段写入和实例字段语义不完整
- `console.log` 直接打 wrapper / 方法数组时会 stringify 失败
- 某些 Java wrapper 不能直接按 Frida 那样 stringify / 反射打印
- 静态 native 方法 hook 存在“不命中”场景
- 内部类 / 反射 / 枚举方法等场景需要继续兼容

这时候一个很重要的认识被逐渐明确了：

- 有些问题是“实现没补齐”
- 有些问题是“Nook 当前执行模型的边界”
- 不能什么都按 Frida 表面 API 去想当然照搬

## 8. 另一个关键阶段：通过 Frida Labs 和文章脚本反向校验语义

项目后来进入非常实战的一段：

- 不再只是自己写 smoke
- 开始直接跑 Frida Labs
- 跑逆向文章里的原始 Frida 脚本
- 再对照 Nook 的行为修正

这里的价值特别大，因为它把开发目标从“功能实现”变成了“兼容真实使用习惯”。

这段时间其实做了三类事：

- 直接兼容能兼容的 Frida 脚本
- 对 Nook 不支持的地方做最小脚本改写
- 记录哪些地方目前仍然是 bug / 语义差异 / 架构边界

这一阶段让项目的目标越来越清楚：

- 不是单纯“做个 Hook 框架”
- 而是“尽量让现成 Frida 思路和脚本在 Nook 上能工作”

## 9. 高热函数和 Hook 引擎热路径问题暴露出来：`strcmp` 白屏/卡顿

随着 Frida 0x8 这种例子反复测试，一个核心性能问题暴露出来了：

- 高热 native 函数，如 `strcmp`
- 在 Nook 上会出现白屏、卡顿、hook 回调报错、甚至长时间阻塞
- Frida 没有这么明显的问题

后面对比 Frida 和 Nook 的实现，逐渐把根因收敛到：

- 每次 hook 触发都要走较重的 dispatch 路径
- 频繁进入 JS runtime
- 递归/重入保护不足
- 缺少 Frida 那种 TLS guard / ignore level / 更快的 bypass path
- enter/leave 路径更重

后面围绕这个问题做了几轮优化，方向基本是参考 Frida：

- thread-local guard
- ignore / bypass 思路
- observer-only 语义
- 减少热路径的同步和无谓 JS 进入

虽然不是把 Frida 完整抄进来，但项目在这个点上开始从“功能做出来”转向“引擎热路径怎么收敛”。

## 10. 对 Frida 的理解也逐渐从“抄 API”变成“抄架构思路”

## 11. 当前状态（2026-05-17）

最近这轮已经收敛到一个比较稳定的点：

- 默认 `spawn + script.js` 已经恢复稳定，真机 hook 能生效
- `session_registry` 现在把 runtime agent 视为权威来源，晚到的 control-ready 不再反向覆盖
- `FinalizeSpawn()` 的失败恢复只回滚 `shell_owner_state` / zygote 事务，不再把恢复态镜像回 `spawn_state`
- 单文件包已经重新构建并推送到设备，设备侧目录只保留 `nook-server` 和日志文件

验证过的回归：

- `build/test_ninjector_spawn_injector.exe`
- `build/test_server_handlers.exe`
- `build/test_server_handlers_spawn_ready_subset.exe`
- `build/test_session_registry_authoritative_preference.exe`

## 12. 下一步

下一步不再是“能不能跑”，而是继续把默认稳定路径和实验路径彻底分层，往真正的 `agent-owned stable spawn` 收敛，同时保留 `--strict-zygote-control` 作为显式实验入口。

### 什么叫“彻底分层”

在 Nook 里，它不是抽象术语，具体指：

- 默认 spawn 只走稳定路径，不因为 server 支持实验能力就自动改路由
- `--strict-zygote-control` 只影响显式实验路径，不回写默认路径的状态
- `runtime-ready` 是权威事实，`control-ready` 只是协作信号
- `spawn_state` 只存兼容/局部状态，不再承担权威所有权
- finalize / rollback 只回滚自己那层的状态，不跨层污染

一句话：每条路径只管自己的生命周期，不能把“我临时知道的事”写成“全局真相”。

到了 step9、step10、step11 这个阶段，项目已经不再只是补 API，而是在分析 Frida 为什么好用：

- 统一的 interceptor 抽象
- per-thread invocation stack
- native 一等公民的 invocation context
- 按需 onLeave trap
- trampoline codegen 分层
- zygote / spawn 的最小控制面设计
- server 单文件部署但内部资产内嵌

也就是说，开发过程到这里出现了一个认知升级：

- 不是“哪个函数没实现”
- 而是“Frida 的优势来自整套架构组织方式”
- Nook 想继续接近 Frida，后面得往这条线演进

## 11. 部署与运行形态开始大改：从多文件走向单 `nook-server`

接下来是最近最重要的一条线：部署面收缩。

一开始 Nook 需要很多文件：

- `nook-server`
- `libnook-agent.so`
- `libncore.so`
- `libc++_shared.so`
- `spawn_markers`
- `spawn_result.json`
- `nook.sock`

这和 Frida 的单 `frida-server` 差距非常大。

于是做了几轮关键改造：

- server / agent 静态链接 C++ runtime，消掉 `libc++_shared.so`
- agent 以内嵌 blob 形式进入 server
- `ncore` 也做内嵌交付
- 默认 IPC 改为 abstract Unix socket，不再依赖文件型 `nook.sock`
- 通过 `memfd_create + /proc/<pid>/fd/<n>` 让 embedded `ncore` 走内存注入路径
- 前台 `./nook-server` 启动时修复相对路径派生，避免 `./libnook-agent.so` 这类错误

这一轮之后，用户视角已经收敛到：

- 只推 `nook-server`
- 不手推 agent / ncore / socket 文件

这是目前项目最明显接近 Frida 的成果之一。

## 12. 但架构上真正最难的点开始浮现：spawn 仍默认依赖 legacy `ncore`

虽然部署上看起来已经接近单 server，但架构上还没完全到 Frida：

- 默认稳定 spawn 仍然走 legacy `ncore`
- 只是 `ncore` 不再要求用户手工部署
- `spawn_markers` / callback file 语义也还没完全消掉

这时候项目的状态发生了一个很重要的变化：

- “部署像 Frida”已经基本做到了
- “spawn 架构像 Frida”还没有彻底做到

这也是现在 step10 文档里最核心的未完成项。

## 13. zygote-control 尝试：真正朝 Frida 的 specialize-path 方向走

为了去掉 `ncore` 主路径，项目开始尝试 zygote-control：

- agent 注入 zygote
- 通过 specialize/fork 路径识别目标 app
- child 侧继承并重建 comm/runtime
- server 只负责控制面

一开始有过错误方向：

- 用 `pthread_atfork(...)` 做 bridge
- 在 zygote 里带入过重的正常 agent / runtime 状态

结果很快暴露出问题：

- `zygote64` 崩溃
- `nativeForkAndSpecialize` 路径出错
- 出现设备重启、系统服务异常、spawn 超时等严重副作用

这是一轮非常典型的“方向是对的，但第一版实现太侵入 zygote”的阶段。

## 14. 关键回退：实验性 zygote-control 改回显式 opt-in

这个阶段的一个很成熟的决策是没有硬顶，而是回退：

- 默认继续 stable legacy spawn
- `zygote-control` 只有 `NOOK_ENABLE_EXPERIMENTAL_ZYGOTE_CONTROL=1` 时才启用

这个决定很关键，因为它说明项目已经开始区分：

- 稳定路径
- 实验路径

而不是把所有新东西都直接推成默认。

## 15. Android 11 specialize-path minimum cut：开始从错误桥接转向真正的 specialize hook

后面又往前推进了一步：

- 不再用 `pthread_atfork` 那套桥
- 开始对 Android 11 的
  - `Zygote.nativeForkAndSpecialize`
  - `Zygote.nativeSpecializeAppProcess`
  做最小切入
- child 侧重建 comm state
- 尽量让 zygote 只保留控制面，child 再恢复正常 runtime

这一版已经明显比之前更接近 Frida 的方向了，但仍然没有到“默认稳定”的程度，尤其：

- 只覆盖当前 Android 11 / 当前设备类
- Android 12+ 的 `ZygoteCommandBuffer.nativeForkRepeatedly(...)` 还没做
- 真机系统环境里还有 Zygisk / MIUI 这类复杂交互因素

## 16. 当前阶段的真实状态

到现在为止，项目已经形成了一个很清晰的状态：

部署层面：

- 很接近 Frida
- 正常使用只需要 `nook-server`

Native / JS / Java 兼容层面：

- 已经能跑大量 Frida 风格测试例子
- 通过了很多 Frida Labs 和文章 case
- 但仍有一些 Java 兼容 bug 和边界

Spawn 架构层面：

- 默认稳定路径仍是 legacy `ncore`
- zygote-control 是实验路径
- 真正 Frida 式 specialize-path 还在收敛

Hook 引擎层面：

- 功能已较完整
- 但高热路径、invocation model、TLS context、onLeave trap、codegen 这些还存在明显优化空间

## 17. 如果要把这个历程概括成几条主线，可以归纳为

- 第一条：从通信骨架到 Host-Server-Agent 三层架构打通
- 第二条：从 Native Hook 原型到 Frida 风格 runtime / CLI / REPL 成形
- 第三条：通过 Frida Labs、文章脚本和真机案例不断反向校验语义
- 第四条：从“功能能用”转向“热路径性能、边界语义和兼容性收敛”
- 第五条：从多文件部署逐步收缩到单 `nook-server`
- 第六条：从 legacy spawn 走向 Frida 式 zygote specialize-path，但目前仍处于稳定化前夜

## 18. 后续写博客时最值得讲的几个转折点

建议重点讲这几个，因为它们最有“开发故事”和“技术判断”的价值：

- 为什么 spawn 从 agent 内等待改成 server 外挂起
- 为什么要用 Frida Labs / 文章脚本来逼兼容，而不是只写自己的 smoke
- 为什么 `strcmp` 这类高热函数把 Hook 引擎热路径问题彻底暴露出来
- 为什么单文件部署并不等于架构上已经完全摆脱 `ncore`
- 为什么 `pthread_atfork` 那条 zygote-control 路走不通，必须转向真正的 specialize hook
- 为什么要把实验路径和默认稳定路径分开

