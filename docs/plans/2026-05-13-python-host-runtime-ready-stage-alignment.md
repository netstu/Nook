# 2026-05-13 Python Host Runtime-Ready Stage Alignment

## Goal

把 `host/nook-py` 的 spawn ready 语义与服务端/C++ host 对齐，避免 Python host
把 control-stage `AGENT_READY` 误判成 spawn 完成。

## Problem

此前 Python 协议层：

- `AgentReady` 没有 `stage`
- `Device.spawn()` 等待任意同 pid 的 `AGENT_READY`

这会导致：

- control-stage `AGENT_READY` 也会被当成最终 ready
- host 侧可能提前返回 `Session`
- 后续 `script create/load` 仍可能因为 runtime 尚未 ready 而失败

这和服务端刚完成的 runtime-ready 收敛目标相冲突。

## Change

更新：

- [protocol.py](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/nook/protocol.py)
- [device.py](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/nook/device.py)
- [test_client.py](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/tests/test_client.py)

具体改动：

- Python 协议层新增 `AgentReadyStage`
  - `RUNTIME = 0`
  - `CONTROL = 1`
- `AgentReady` 增加 `stage` 字段
- `encode_agent_ready()` / `decode_agent_ready()` 支持 TLV field `6`
- `Device._take_agent_ready()` 现在只消费 runtime-stage ready

## Regression Coverage

新增：

- `test_spawn_ignores_control_stage_agent_ready`

覆盖点：

- 收到 `SPAWN_RESPONSE`
- 仅收到 control-stage `AGENT_READY`
- `device.spawn()` 不应成功返回
- 应按 agent-ready 阶段超时报错

## Verification

Passed:

```powershell
python -m unittest host.nook-py.tests.test_client
```

## Result

现在三层语义一致：

- 服务端 readiness 看显式 runtime-ready state
- C++ host spawn client 只消费 runtime-stage `AGENT_READY`
- Python host spawn 也只消费 runtime-stage `AGENT_READY`

这一步完成后，host 侧不会再把 control-stage ready 提前暴露成可建脚本的会话。
