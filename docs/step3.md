 待完善项

  ┌────────┬────────────────────────────────────┬───────────────────────────────────────────────────────────────────┐
  │ 优先级 │                问题                │                               说明                                │
  ├────────┼────────────────────────────────────┼───────────────────────────────────────────────────────────────────┤
  │ 高     │ RPC Server/Agent 侧缺失            │ Python SDK 有 call_rpc()，但 C++ Server/Agent 没有 RPC_REQUEST    │
  │        │                                    │ 处理器                                                            │
  ├────────┼────────────────────────────────────┼───────────────────────────────────────────────────────────────────┤
  │ 中     │ post/unload 命令仍不 resume        │ 可能是设计如此，但需要文档说明                                    │
  ├────────┼────────────────────────────────────┼───────────────────────────────────────────────────────────────────┤
  │ 低     │ --interactive 无法优雅退出 stdin   │ daemon 线程，问题不大                                             │
  │        │ 线程                               │                                                                   │
  └────────┴────────────────────────────────────┴───────────────────────────────────────────────────────────────────┘

---
  下一步

  1. 补充 Server 侧 RPC 处理 (优先)

  当前 server_handlers.cpp 没有 kRpcRequest 处理器。需要：

  // server_handlers.cpp
  void HandleRpcRequest(SessionRegistry* registry,
                        comm::Session& session,
                        const comm::Frame& frame) {
      // 解码 RPC 请求
      // 转发给 Agent
      // Agent 执行后返回 RpcResponse
  }

  2. Agent 侧脚本 RPC 支持

  需要 QuickJS 集成后才能真正执行 RPC：

  // hook.js
  rpc.exports = {
      ping: function(name) {
          return { message: "pong", name: name };
      }
  };

  3. 真机验证清单

  # 基础验证
  nook-cli --usb apps
  nook-cli --usb ps

  # spawn + resume + script (已修复)
  nook-cli --usb spawn com.demo.target -l hook.js --resume

  # attach + script
  nook-cli --usb attach com.demo.target -l hook.js

  # 持续监听
  nook-cli --usb spawn com.demo.target -l hook.js --resume --wait

  # JSON 输出
  nook-cli --usb spawn com.demo.target --json

  4. 文档收尾

  - README 快速上手
  - CLI 命令参考
  - API 文档

---
  建议：先做真机验证 spawn -l --resume 是否修复，然后再考虑 RPC 和 QuickJS 集成。