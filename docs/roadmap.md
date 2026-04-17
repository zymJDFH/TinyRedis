# TinyRedis Roadmap

## 文档定位

本文件只描述 TinyRedis 的演进方向与优先级，不包含具体日期。  
任务拆解、状态流转与排期统一在 GitHub `Issues/Projects` 中维护。

## 项目目标

- 构建一个可运行、可测试、可演进的 Redis 核心子集实现。
- 在保持分层清晰的前提下，逐步补齐协议、命令、存储与工程化能力。
- 优先保证正确性与稳定性，再推进性能与高级特性。

## 当前基线

- 网络层：单线程 `epoll`（LT）事件循环可用。
- 协议层：RESP2 基础解析与编码可用。
- 命令链路：`PING/SET/MSET/GET/MGET/DEL/EXISTS/INCR/INCRBY/DECR/EXPIRE/TTL/PTTL/PERSIST` 已打通。
- 过期机制：惰性过期 + 主动过期扫描（cron 抽样）可用。
- 存储层：`InMemoryDB + DICT + SDS + RedisObject` 基础能力可用。
- 测试：`test_sds`、`test_dict`、`test_resp`、`test_command` 已接入 CTest。

## Now（当前优先）

- 补齐命令与协议边界测试：非法输入、半包/粘包、错误语义一致性。
- 补充端到端测试（`redis-cli`/脚本）覆盖真实请求链路。
- 整理命令执行链路的错误处理与返回格式，减少隐式行为。
- 完善文档与目录职责说明，保持实现与文档一致。
- 建立基础质量门禁：本地默认跑通 `ctest --output-on-failure`。

## Next（下一阶段）

- 增强对象与存储抽象，降低 `command` 与底层实现耦合。
- 扩展命令子集（按优先级逐步补齐 String 常用命令，如 `DECR/INCRBY/MGET/MSET/SET` 选项）。
- 初步引入持久化骨架（AOF 追加写入与最小恢复链路）。

## Later（中长期）

- 持久化：AOF 写入与重放恢复。
- 更多数据类型：List/Hash/Set/ZSet 核心子集。
- 稳定性与可观测性：慢命令、基础指标、故障定位信息。
- 性能基线与回归对比（吞吐/延迟）。

## 完成标准（Definition of Done）

- 功能有对应测试，且 `ctest --output-on-failure` 通过。
- 行为与错误语义符合当前文档约定。
- 关键变更同步更新 README/设计文档。
- 代码通过基本自检（编译、核心路径验证）。

## 跟踪方式

- 任务来源：GitHub Issues。
- 状态管理：GitHub Projects（`Todo / In Progress / Done`）。
- 本文档只保留长期有效的方向与优先级，不记录日常进度细节。
