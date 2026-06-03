结果已经说明当前状态很明确：

  - attach ... -l hook.js --usb 已经打通
      - attach ok
      - script create ok
      - script load ok
  - spawn ... --resume --usb 也已经打通
      - spawn response ok
      - agent ready
      - resume ok

  所以现在可确认：

  - Python SDK 通信链路正常
  - CLI 的 --usb 工作正常
  - attach 工作流可用
  - spawn + resume 工作流可用

  剩下唯一没打通的是：

  - spawn -l hook.js --resume --usb

  也就是“spawn 后在 resume 前预加载脚本”这条链路。这个已经不是基础通信问题，而是当前 server/agent 的 suspended 语义和脚
  本创建时机问题。

  你现在 review 可以先认为：

  - 基础 Python SDK 已可用
  - CLI 基础命令已可用
  - 真机可用链路至少有两条：
      - attach -l
      - spawn --resume

  下一步我建议直接修：

  - spawn -l ... --resume
    优先方案是把 CLI 改成：

  1. spawn
  2. 等 agent ready
  3. resume
  4. 再 attach/load script

  这样能先保证功能可用，再考虑后面要不要把“挂起态预加载脚本”做成真正的 Frida 式语义。
  如果你同意，我就按这个方案继续改。