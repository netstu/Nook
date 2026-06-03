     Nook 目前更像“面向 Android/arm64 的专项 Hook 框架 + 正在补齐 Frida 常用脚本能力的实验平台”，还不是一个真正能和
     Frida 同层竞争的通用动态插桩平台。
    
     我先说结论，再说依据。
    
     一、总体定位上的不足
    
     和 Frida 比，Nook 当前最大的不足不是“某一个函数没实现”，而是整体层级还低一层：
    
     1. 通用性不足
     Frida 是跨平台、跨架构、跨语言运行时的通用动态插桩平台。
     Nook 目前明显偏 Android/arm64 场景，仓库自己也写了：
     - arm64 inline hook 支持
     - arm32 inline hook 还不支持
     - x86/x86_64 不是当前目标
     依据：
     - README: 13-16, 440-451 行附近
    
     这意味着它现在更适合“做 Android Native/Java Hook 研究和定向实战”，而不是像 Frida 那样拿来即用、到处都能插。
    
     2. 还是偏“研究型/开发型”，不是成熟产品型
     README 自己承认：
     - examples 更接近 research/development usage
     - 还不是 polished SDK packaging
     依据：
     - README: 444-445 行
    
     Frida 的强项之一就是产品化程度高：frida-server / gadget / CLI / Python bindings / JS API / trace
     生态都是成体系的。
     Nook 现在虽然已经有 host/nook-py 和 nook-cli，但整体还比较“工程中”。
    
     二、Native Hook 抽象不如 Frida 统一
    
     这是我觉得最核心的技术短板之一。
    
     1. NookNativeHook 不是统一 Native Hook 抽象
     仓库 README 明写：
     - NookNativeHook 当前只走 PLT hook 路径
     - 如果想要 inline，需要直接调用 NookInlineHook*
     依据：
     - README: 72-79 行
     - include/nook/NookNativeHook.h
     - src/framework/NookNativeHook.cpp
    
     我还看了实现：
     - NookNativeHookInitialize -> NookPltHookInitialize()
     - NookNativeHookHookSymbol -> NookPltHookSymbol(...)
     也就是说这个 facade 现在基本只是 PLT 包装，不是真正的“Native Hook 总入口”。
    
     对比 Frida：
     - Frida 用户通常不需要先区分“PLT Hook / Inline Hook / deferred symbol hook”这些实现细节
     - 你更多是围绕 Interceptor.attach / replace / Module / NativeFunction 这套统一抽象工作
    
     Nook 这里暴露出的实现细节太多，使用心智负担比 Frida 大。
    
     2. deferred hook 依赖额外 probe 机制，复杂度高
     Nook 的 deferred inline hook 依赖：
     - linker observer
     - hook soinfo::call_constructors()
     - 额外的 libnook_inline_observer_probe.so 去探测 soinfo 偏移
     依据：
     - README: 324-353 行
    
     这说明它在“模块延迟加载再 Hook”这个场景下是能做，但路径比较重，也比较依赖 Android linker 内部结构。
    
     对比 Frida：
     - Frida 在开发者体验上通常更自然，脚本层完成度更高
     - 用户一般不会感知到这么多底层补偿逻辑
    
     换句话说，Nook 能做，但“实现成本和维护成本明显高于 Frida 的用户体验”。
    
     三、JS Runtime 和脚本生态明显弱于 Frida
    
     Nook 这块已经在努力往 Frida 靠，但距离还是很大。
    
     1. 当前 JS API 还是“最小可用子集”
     README 自己用了 very explicit 的措辞：
     - “first minimal JavaScript-facing native hook bridge”
     - only type: "inline" is implemented
     - Module.findExportByName 是已实现
     - NativePointer 只有最小方法集
     - Interceptor.attach / detach / detachAll 有
     依据：
     - README: 246-308 行
    
     这说明它现在只是把 Frida 常用模型的一个“最小子集”做出来了，不是完整 Frida JS Runtime。
    
     2. 缺少 Frida 的大量高阶能力
     从仓库文档和测试映射文档看，当前明确缺口包括：
    
     - 没有 Stalker 对等能力
     - 没有 frida-trace 风格 CLI
     - 没有 jnitrace / jtrace 这一类自动化 tracer 工具层
     - 没有 objection 风格交互式命令层
     依据：
     - tests/Test_Lab/nook-frida-articles/04-tooling-and-gaps.md: 31-35, 51-55, 61-64, 89-117 行
    
     这其实很关键。因为 Frida 的强并不只是“能 Hook”，而是“能快速探索目标”：
     - trace 一批函数
     - 快速列类、列实例、列方法
     - stalk 指令流/基本块
     - 利用成熟社区脚本快速干活
    
     Nook 当前更像“我能把一些常见操作做出来”，但缺少 Frida 那种高效率的探索式工作流。
    
     3. 脚本运行时仍然非常重、复杂，而且可维护性风险更高
     我看了两个核心文件：
     - src/agent_runtime/js_runtime.cpp：21198 行
     - src/agent_runtime/nook_native_js_bridge.cpp：2165 行
    
     尤其 js_runtime.cpp 已经非常大，这通常意味着：
     - 功能在不断堆叠
     - 运行时职责很多
     - 后续维护、回归测试、行为一致性会越来越困难
    
     而 Frida 的优势之一，是它的运行时抽象和 API 设计已经沉淀很多年，边界清晰、生态成熟。
     Nook 现在这块很可能还会持续遇到：
     - 行为兼容性问题
     - 边界 case 很多
     - 新增 API 时回归成本高
    
     四、CLI / 工具链体验不如 Frida 完整
    
     1. 有 Frida-like CLI，但还不是 Frida-level CLI
     host/nook-py/README 里已经在刻意对齐 Frida 使用方式：
     - nook-cli -U -f com.demo.target -l hook.js
     - nook-cli -U com.demo.target -l hook.js
     - 输出风格也模仿 [*] [+] [!] [-]
     依据：
     - host/nook-py/README: 106-124, 194-204 行
     - host/nook-py/nook/cli.py: 26-32, 163-225 行
    
     这说明项目方向是明确“向 Frida UX 靠拢”的。
    
     但问题是：
     - 目前 CLI 还偏最小实现
     - README 自己写了 advanced CLI ergonomics 还没做好
     - 设备侧 server 需要先手工准备，不是 fully seamless
     依据：
     - host/nook-py/README: 28-32, 48-88 行
    
     Frida 的成熟度在于：
     - attach/spawn/workflow 更统一
     - tracing / scripting / rpc / tools 互相配合顺畅
     - 大量第三方工具默认就围绕 Frida 做
    
     Nook 现在 CLI 能用，但更像“研发中的宿主工具”，不是成熟生产力工具。
    
     2. 缺少完整外围生态
     Frida 的实际价值很大一部分来自生态：
     - objection
     - frida-trace
     - 大量 code snippets
     - 大量现成 bypass 脚本
     - 逆向社区默认支持
    
     Nook 现在仓库里虽然有很多 smoke js / labs / article
     scripts，但这些还属于“项目自带案例”，不是外部生态网络效应。
    
     这类差距不是补几个 API 就能解决的。
    
     五、平台覆盖与架构覆盖弱
    
     从当前仓库能看到的真实状态：
    
     1. inline hook 主要支持 arm64
     README 明写：
     - Inline Hook (arm32) not supported yet
     依据：
     - README: 13-14, 442 行
    
     2. 本项目主开发目标就是 arm64-v8a
     依据：
     - README: 5, 89 行
    
     3. 非 Android 场景基本不是重点
     很多设计强依赖：
     - ART / JNI
     - Android linker
     - soinfo
     - xdl
     - zygote / spawn gate
     - adb/usb workflow
    
     所以它和 Frida 的差距，不只是“功能点多少”，而是“平台战略宽度”完全不同。
    
     六、自动化追踪/分析能力不足
    
     对比 Frida，Nook 当前在“探索型分析”这块明显偏弱：
    
     1. 缺少批量 trace 能力
     文档里已经明确承认没有 frida-trace 风格 CLI。
     这意味着：
     - 单点 Hook 可以
     - 大规模函数自动跟踪不方便
     - 做 JNI 调用链、批量入口分析时效率会低很多
    
     2. 缺少重型执行跟踪
     没有 Stalker 对等能力，这个缺口是很大的。
     因为 Frida 在高阶场景里不仅是 Hook API，还能做：
     - 基本块追踪
     - 指令级流分析
     - 动态执行画像
    
     Nook 现在离这个层面还有很远。
    
     七、产品稳定性和接口稳定性还不够
    
     从 README 能看出很多“当前语义”“当前 workflow”“当前 validated smoke target”之类表述，这通常说明
     API/行为还在收敛中。
    
     比如：
     - current semantics
     - current validated smoke target
     - current workflow
     - sample runtime glue is not a stable framework contract
     依据：
     - README: 243-245, 285, 356 等附近
    
     这说明：
     1. 接口还没完全固化
     2. 示例和框架边界还不够稳定
     3. 用户写的 payload/脚本 可能更容易被后续内部调整影响
    
     而 Frida 的优势恰恰是：
     - API 稳定很多年
     - 教程、脚本、用户认知都很统一
     - 升级虽然有兼容问题，但总体是成熟平台节奏
    
     八、安全性/远程控制面成熟度暂时看不出优势
    
     我简单看了 communication/host/agent 这部分结构，能看到：
     - tcp transport
     - unix transport
     - session / protocol / frame / tlv
     - spawn / attach / resume / detach / post / unload
    
     但我没有从文档中看到比较成熟的：
     - 明确认证机制
     - 完整权限控制
     - 传输安全设计说明
     - 多租户/隔离能力
    
     这未必代表它没有做，只是至少从当前项目表层来看，这不是它目前最成熟、最突出的部分。
    
     对比 Frida：
     Frida 也不是“安全平台”，但它的使用模型、部署方式、社区认知已经成熟很多。
     Nook 作为自建控制通道，后面要走向更广泛使用时，这块会是必须补的。
    
     九、从工程形态看，Nook 更像“专项替代尝试”，还不是 Frida 完整替身
    
     如果一句话概括：
     Nook 现在已经具备了“在 Android/arm64 上复现 Frida 常见 Java/Native Hook 工作流的一部分能力”，但还不具备 Frida
     那种：
     - 通用插桩平台能力
     - 完整脚本运行时
     - 重型动态分析能力
     - 工具链生态
     - 跨平台产品成熟度
    
     十、我认为最重要的几个不足，按优先级排
    
     如果站在“和 Frida 对比”的角度，我会把不足按重要性排成这样：
    
     1. Native Hook 抽象不统一
     - NookNativeHook 实际只走 PLT
     - inline 还要用户显式区分
     这是架构层面的不足。
    
     2. JS Runtime 只到最小子集
     - 目前只是 Frida API 的局部模拟
     - 很多高阶能力没有
    
     3. 缺少 trace/stalker 类重型分析能力
     - 没有 Stalker
     - 没有 frida-trace / jnitrace 风格工具层
    
     4. 平台/架构覆盖窄
     - 重点只有 Android/arm64
     - arm32 还不支持
     - 非 Android 基本不成立
    
     5. 产品化和生态不足
     - CLI/SDK 仍在补
     - 文档、案例、外围工具还没形成网络效应
    
     6. deferred/observer 机制工程复杂度高
     - 对 Android linker 内部结构依赖重
     - 长期维护成本高
    
     如果你要我再更直白一点：
    
     Frida 的优势是“抽象好、生态强、探索效率高、平台广”；
     Nook 当前的优势更像“自己可控、能按特定 Android 场景做深、底层实现能自己改”。
    
     所以 Nook 的不足，不是“不能 Hook”，而是“还没有把 Hook 这件事做成 Frida
     那样成熟、统一、通用、低心智负担的动态插桩平台”。
    
## 2026-05-10 zygote-control default regression

- Device crash logs confirmed the current experimental zygote-control path is still unsafe on the target MIUI/Android 11 device when enabled in `zygote64`.
- The fatal path is not a normal spawn timeout. `zygote64` aborts inside `ForkCommon -> FileDescriptorTable::Restat` with:
  - `JNI FatalError called: (zygote) Unable to get socket name`
  - stack also shows `/data/adb/modules/zygisksu/lib64/libzygisk.so`
- Root cause for the regression was a code/config mismatch:
  - `docs/step10.md` already documented zygote-control as explicit opt-in via `NOOK_ENABLE_EXPERIMENTAL_ZYGOTE_CONTROL=1`
  - but `server/ninjector_spawn_injector.cpp` had drifted to "enabled unless env == 0"
- Fix:
  - restore default behavior to legacy spawn
  - only enable zygote-control when `NOOK_ENABLE_EXPERIMENTAL_ZYGOTE_CONTROL=1`
- Practical consequence:
  - normal testing should not touch the zygote-control path anymore
  - this avoids repeated `zygote64` aborts, device service restarts, and reboot-like secondary failures after stopping `nook-server`

## 2026-05-10 attach explicit-init refactor

- Device logs for `attach com.ad2001.frida0x8` showed that remote injection was no longer failing at raw `dlopen`, but the target still timed out at:
  - `NookAgentInitialize begin process=com.ad2001.frida0x8`
  - `runtime bridge ensure begin process=com.ad2001.frida0x8`
  - then no `runtime bridge ensure ok`
  - no `AGENT_READY sent`
  - server side ended with `attach agent-ready timeout`
- Root cause:
  - attach was still relying on the agent constructor path to run full initialization during remote `dlopen`
  - on the tested Android 11 device this constructor-time path was not stable in ptrace-injection context
  - the raw loader return value was also not authoritative: logs showed `NookAgentInitialize` could start even when remote `dlopen` reported failure details like `undefined symbol: JNI_OnLoad`
- Fix:
  - add `NOOK_SKIP_AUTO_INIT=1` support in `src/framework/NookComm.cpp`
  - before attach-side `dlopen`, set that env var in the remote process
  - load the agent image without letting the constructor run the full bridge/bootstrap path
  - after load, explicitly invoke `NookAgentInitialize`
  - do not depend on `handle + dlsym` for this stage when the loader handle is unreliable; resolve `NookAgentInitialize` from the agent ELF export offset and map it to the remote module base
  - clear `NOOK_SKIP_AUTO_INIT` after the explicit init stage
- Result:
  - attach for `com.ad2001.frida0x8` became stable again
  - the tested `script2.js` native hook path resumed working
  - this is closer to Frida's load/init separation and removes one major constructor-time timing hazard from attach
- Files changed:
  - `src/framework/NookComm.cpp`
  - `server/ninjector_compat.cpp`
- Current boundary after this fix:
  - attach is now on an explicit-init model
  - stable spawn is still on the legacy `ncore` backend
  - therefore this fix does not yet remove the need for `libncore.so` in stable spawn, and it does not yet make `libnook-agent.so` a removable runtime artifact
