# 2026-05-13 Host Runtime-Ready Timeout Alignment

## Goal

把 host 层超时文案与当前真实等待语义对齐，避免继续把 runtime-ready 等待误写成 authoritative-ready。

## Problem

此前 host 层在 spawn 第二阶段超时时仍使用：

- `wait authoritative agent ready timed out`

但当前实际等待的是：

- runtime-stage `AGENT_READY`

这会在调试 `spawn -> script create/load` 链路时制造概念混淆：

- authoritative-ready 更偏 control/RPC ready
- runtime-ready 才是脚本可装载的真正边界

## Change

更新：

- [host_spawn_client.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/src/communication/host/host_spawn_client.cpp)
- [device.py](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/nook/device.py)
- [test_host_spawn_client.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/tests/communication/test_host_spawn_client.cpp)
- [test_client.py](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/tests/test_client.py)
- [test_cli.py](/E:/Learn/my_program/all_my_hook/kanxue/Nook/host/nook-py/tests/test_cli.py)

文案统一为：

- `wait runtime agent ready timed out`

## Verification

Passed:

```powershell
python -m unittest host.nook-py.tests.test_client host.nook-py.tests.test_cli
```

## Result

现在 host 层看到的两个 spawn timeout phase 为：

- `wait spawn response timed out`
- `wait runtime agent ready timed out`

这与当前 owner-state 收敛后的真实边界一致。
