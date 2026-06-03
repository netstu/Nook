# 2026-05-12 Symbi TStub log_print Removal Status

## 本次改动

已从 `symbi` zygote stub ABI 中移除 `log_print`：

- [stub.h](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/symbi/stub_src/stub.h)
- [offset_check.c](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/symbi/stub_src/offset_check.c)
- [symbi_injector_local.cpp](/E:/Learn/my_program/all_my_hook/kanxue/Nook/server/symbi_injector_local.cpp)

## 判断依据

`log_print` 当前只被 stub 内部日志宏使用：

- `child hit, waiting for host ack`
- `host ack received...`
- `raise(SIGSTOP) unavailable`
- `Host ack handshake failed`

它不参与：

- callback socket 握手
- child pid/ppid 上报
- `SIGSTOP` gating
- slot restore
- server 侧后端路由

所以它是纯 debug 字段，不是运行必要字段。

## 现在的处理

现在改成：

- `TStub` 不再携带 `log_print`
- `STUB_LOGI/STUB_LOGE` 退化为编译期空操作
- server 侧不再解析 `__android_log_print`
- 写入 stub 配置时也不再填这个字段

## 影响

这会让 zygote stub 自身少一项远程符号依赖，直接收益是：

1. `collect_symbi_context()` 依赖面继续收缩
2. `TStub` 继续瘦身
3. 后续对齐 Frida 的“最小 zygote payload”更容易

代价是：

- 失去 stub 内部的 Android log 输出

但这部分日志本来就不是主链稳定性所必需。

## 后续建议

下一步不建议继续删 `raise`，因为当前 child stop/gate 仍依赖它。

更合适的后续方向有两个：

1. 检查 `getpid/getppid` 是否都必须保留，还是可压成更小的 callback 语义
2. 开始把 `symbi` 的 stop window 再缩短，尽量把 zygote 停住期间的工作继续前移
