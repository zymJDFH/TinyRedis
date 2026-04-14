# TinyRedis

一个 C++ TinyRedis 实现。

## 文档

- [开发路线图](docs/roadmap.md)
- [工业化实现方案](docs/industrial-plan.md)
- [P0-P1 执行拆解](docs/p0-p1-execution-plan.md)
- [里程碑看板模板](docs/milestones.md)
- [设计说明](docs/design.md)
- [架构决策记录](docs/decisions.md)
- [RESP 协议笔记](docs/protocol-notes.md)
- [测试策略](docs/testing.md)

## 性能摘要

- 本机回环、`redis-benchmark`、单线程 `epoll`（LT）版本：
- `P=1`：约 `17w QPS`
- `P=100`：`SET` 约 `68w QPS`，`GET` 约 `90w QPS`
- 详细参数与说明见 [测试策略](docs/testing.md)
