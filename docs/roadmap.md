# TinyRedis 路线图

说明：如果目标是“工业级可上线实现”，请优先阅读 [工业化实现方案](industrial-plan.md)。

## 目标

构建一个小而完整的类 Redis 服务，用来学习：

- Redis 的设计思想
- C++ 系统编程能力
- AI 协作编程工作流

## 当前进度

- `SDS`：基础动态字符串已实现。
- `DICT`：基础哈希表 API（`set/get/erase`）已实现。
- `RESP`：编码器/解析器与基础测试已就位。
- `Command`：`PING/SET/GET/DEL/EXISTS/INCR` 已实现并有单元测试。
- `Server`：最小 TCP 服务已可被 `redis-cli` 连接验证。

## 里程碑

### M1：命令层（已完成）

- 将 RESP Array 解析为命令 `argv`。
- 增加命令分发器（dispatcher）。
- 支持 `PING`、`SET`、`GET`、`DEL`。

验收标准：

- 能解析 `*2\r\n$4\r\nPING\r\n$4\r\nPONG\r\n` 这类输入。
- 对已支持命令返回正确 RESP 回复。

### M2：内存数据库核心（进行中）

- 使用 `DICT + SDS` 作为 key/value 存储引擎。
- 明确 value 所有权与生命周期模型。
- 增加基础过期机制（TTL，先做惰性删除）。

验收标准：

- `SET/GET/DEL/EXISTS/INCR` 测试通过。
- 过期 key 不会被返回。

### M3：TCP 服务循环（进行中）

- 已实现单线程 TCP 版本（可与 `redis-cli` 通信）。
- 已升级为 `epoll`（LT）事件循环，支持多连接并发处理。
- 下一步考虑 `EPOLLET`（ET）版本对比与优化。
- 持续保持协议正确性优先于性能优化。

验收标准：

- 可通过 socket 跑通端到端 CLI 测试。

### M4：Redis 风格增强

- 渐进式 rehash。
- value 对象抽象。
- 基础 AOF（append-only）持久化。

验收标准：

- 每个特性能说明设计取舍，并提供对应测试。

## 开发规则

- 一次只推进一个里程碑。
- 每个功能必须先有测试，再继续下一个功能。
- 任何非平凡设计取舍都记录到 `docs/decisions.md`。
