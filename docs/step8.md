● 找到问题了！对比 Frida 和 Nook 的实现：

---
  Frida vs Nook 高频 Hook 性能对比

  问题根因

  Nook 的 DispatchInlineHookSlot 每次调用都会：
  1. 获取 slot 快照（mutex lock）
  2. 创建 HookEvent 结构
  3. 获取 hook record（mutex lock）
  4. 调用 JsRuntime::InvokeNativeHookCallbackSync（同步进入 JS）
  5. 调用原函数后，再次创建 leave event
  6. 再次调用 JS

  没有任何快速跳过机制！

  Frida 的关键优化

  // guminterceptor.c:1400-1435
  gboolean _gum_function_context_begin_invocation(...) {
      // 1. Guard Key 检查 - 防止递归调用
      if (gum_tls_key_get_value(gum_interceptor_guard_key) == interceptor) {
          *next_hop = function_ctx->on_invoke_trampoline;
          goto bypass;  // 直接跳过，不进 JS
      }
      gum_tls_key_set_value(gum_interceptor_guard_key, interceptor);

      // 2. ignore_level 检查
      if (invoke_listeners) {
          invoke_listeners = (interceptor_ctx->ignore_level <= 0);
      }
    
      // 3. 只有 invoke_listeners = true 时才进入 JS
      if (invoke_listeners) {
          for (i = 0; i != listener_entries->len; i++) {
              listener_entry->listener_interface->on_enter(...);
          }
      }

  bypass:
      // 快速路径：直接调原函数
  }

  对比表

  ┌──────────────────────┬────────────────────────┬─────────────────┬──────────────────────────────────────────┐
  │         机制         │         Frida          │      Nook       │                   影响                   │
  ├──────────────────────┼────────────────────────┼─────────────────┼──────────────────────────────────────────┤
  │ Guard Key (递归保护) │ ✅ TLS-based           │ ❌ 无           │ strcmp 在 JS 内部被调用时会再次触发 hook │
  ├──────────────────────┼────────────────────────┼─────────────────┼──────────────────────────────────────────┤
  │ ignore_level         │ ✅ 每线程忽略计数      │ ❌ 无           │ 无法临时禁用 hook                        │
  ├──────────────────────┼────────────────────────┼─────────────────┼──────────────────────────────────────────┤
  │ 快速路径             │ ✅ native 检查后跳过   │ ❌ 每次都进 JS  │ 高频函数卡顿                             │
  ├──────────────────────┼────────────────────────┼─────────────────┼──────────────────────────────────────────┤
  │ Mutex                │ 最小化，部分 lock-free │ 多个 mutex/调用 │ 竞争开销                                 │
  └──────────────────────┴────────────────────────┴─────────────────┴──────────────────────────────────────────┘

  为什么 strcmp 特别卡

  App 启动
    └── 系统/库初始化
          └── 调用 strcmp 数百/千次
                └── 每次都:
                      1. mutex lock (slot)
                      2. 创建 HookEvent
                      3. mutex lock (record)
                      4. 进入 JS runtime
                      5. 执行 onEnter callback
                      6. 退出 JS
                      7. 调原函数
                      8. 再进 JS 执行 onLeave
                      9. 退出 JS

  而 Frida：
  App 启动
    └── 系统/库初始化
          └── 调用 strcmp 数百/千次
                └── 检查 guard key
                      └── 已设置? → 直接 bypass (纳秒级)
                      └── 未设置? → 正常处理

---
  优化建议

  方案 1: 添加 Thread-Local Guard（最重要）

  // nook_native_js_bridge.cpp

  thread_local bool g_in_hook_dispatch = false;

  uint64_t DispatchInlineHookSlot(size_t slot_index, ...) {
      // 快速检查：已在 hook 处理中，直接调原函数
      if (g_in_hook_dispatch) {
          NativeJsInlineHookSlotState slot = {};
          if (GetInlineHookSlotSnapshot(slot_index, &slot) && slot.original_function) {
              auto original = reinterpret_cast<InlineHookReplacementFunction>(slot.original_function);
              return original(x0, x1, x2, x3, x4, x5, x6, x7);
          }
          return 0;
      }

      g_in_hook_dispatch = true;  // 设置 guard
      // ... 原有逻辑 ...
      g_in_hook_dispatch = false; // 清除 guard
      return return_value;
  }

  方案 2: 添加 ignore_level

  thread_local int g_hook_ignore_level = 0;

  void IgnoreCurrentThread() { g_hook_ignore_level++; }
  void UnignoreCurrentThread() { g_hook_ignore_level--; }

  uint64_t DispatchInlineHookSlot(...) {
      if (g_in_hook_dispatch || g_hook_ignore_level > 0) {
          // 快速 bypass
      }
      // ...
  }

  方案 3: 减少 Mutex 开销

  // 使用 atomic 快照代替 mutex
  std::atomic<NativeJsInlineHookSlotState*> slot_snapshots[kMaxSlots];

  // 或者使用 RCU-style 读取

  方案 4: 条件过滤（可选）

  类似 Frida 的 unignorable listener，允许脚本标记某些 hook 为"可忽略"：

  Interceptor.attach(strcmp_addr, {
      ignorable: true,  // 允许在 guard 状态下跳过
      onEnter: function(args) { ... }
  });

---
  优先级建议

  ┌────────┬────────────────────┬──────────────────┬────────┐
  │ 优先级 │        方案        │     预估效果     │ 改动量 │
  ├────────┼────────────────────┼──────────────────┼────────┤
  │ P0     │ Thread-Local Guard │ 解决 90% 卡顿    │ 小     │
  ├────────┼────────────────────┼──────────────────┼────────┤
  │ P1     │ ignore_level       │ 允许更细粒度控制 │ 小     │
  ├────────┼────────────────────┼──────────────────┼────────┤
  │ P2     │ 减少 Mutex         │ 进一步降低延迟   │ 中     │
  ├────────┼────────────────────┼──────────────────┼────────┤
  │ P3     │ 条件过滤           │ 用户可选         │ 中     │
  └────────┴────────────────────┴──────────────────┴────────┘

  先实现 Thread-Local Guard 就能解决你遇到的 strcmp 卡顿问题。