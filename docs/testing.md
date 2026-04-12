# 测试策略

## 测试层级

- 单元测试：`SDS`、`DICT`、`RESP`。
- 集成测试（计划中）：命令执行链路。
- 端到端测试（计划中）：通过 TCP socket 验证整体行为。

## 当前常用命令

构建：

```bash
cmake -S . -B build
cmake --build build -j4
```

运行测试：

```bash
cd build && ctest --output-on-failure
```

基准测试（示例）：

```bash
redis-benchmark -h 127.0.0.1 -p 6379 -n 100000 -c 50 -P 1 -t set,get
redis-benchmark -h 127.0.0.1 -p 6379 -n 100000 -c 50 -P 100 -t set,get
```

## 性能基准（本机回环）

- 测试对象：当前单线程 `epoll`（LT）版本。
- 测试工具：`redis-benchmark`。
- 说明：`redis-benchmark` 会尝试 `CONFIG`，当前未实现该命令，会有告警但不影响 `SET/GET` 压测结果。

结果记录（你的实测）：

- `P=1`（更接近日常交互）：上限约 `17w QPS`。
- `P=100`（管道化吞吐上限）：`SET` 约 `68w QPS`，`GET` 约 `90w QPS`。

结论：

- `P=1` 用于评估单请求真实能力。
- `P=100` 用于评估 pipeline 场景吞吐极限。
- 后续对比 `ET` 版本时，建议保持同一参数集（`n/c/P`）做横向比较。

## 覆盖重点

### SDS

- 构造、append、clear、移动语义。
- 大 payload 下的扩容行为。

### DICT

- set/get/erase 正确性。
- 已存在 key 的更新行为。
- rehash 实现后的行为验证。

### RESP

- 基础类型编码与解码。
- 分包/半包解析。
- 单缓冲区多消息连续解析。
- 非法协议输入处理。

### Command

- `PING/SET/GET/DEL/EXISTS/INCR` 正常路径。
- 参数个数错误与未知命令错误。
- `INCR` 的非整数输入与溢出错误处理。

## 完成定义（Done Definition）

一个功能仅在以下条件满足时视为完成：

- 有测试覆盖。
- 本地测试通过。
- 若涉及非平凡取舍，已记录到 `docs/decisions.md`。
