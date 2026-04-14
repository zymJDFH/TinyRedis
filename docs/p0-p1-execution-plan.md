# TinyRedis P0-P1 执行拆解（4 周）

更新时间：2026-04-13

## 1. 目标范围

- P0（第 1-2 周）：工程化基线、模块边界、可测试性提升。
- P1（第 3-4 周）：对象模型与 TTL（惰性过期）闭环。

## 2. 交付清单

- 新增对象层（`object`）与 DB 抽象层（`db`）。
- 命令层接入对象 DB，不再直接操作 `unordered_map`。
- 新增 `EXPIRE/TTL/PTTL/PERSIST`。
- 新增 TTL 单测、命令集成测试、socket 端到端测试。
- 新增 1 篇 ADR：对象生命周期与所有权。

## 3. 文件级任务拆解

## 3.1 Week 1（P0-W1）：抽象边界落地

- 新增 `include/object/redisObject.hpp`：定义对象类型、编码、基础元数据。
- 新增 `src/object/redisObject.cpp`：对象创建/释放/类型检查工具函数。
- 新增 `include/db/database.hpp`：统一 DB 接口（set/get/del/exists/incr/expire/...）。
- 新增 `src/db/database.cpp`：最小实现（先 String + 无过期）。
- 修改 `include/command/commandDispatcher.hpp`：依赖 DB 抽象，不持有具体容器。
- 修改 `src/command/commandDispatcher.cpp`：改为调用 DB 接口。
- 更新 `CMakeLists.txt`：加入 `object`、`db` 编译目标。

验收：

- 现有 `test_command` 全绿。
- 未引入跨层 include 反向依赖。

## 3.2 Week 2（P0-W2）：工程化与验证基线

- 新增 `test/test_db.cpp`：DB 基础语义测试（set/get/del/exists/incr）。
- 新增 `test/test_e2e_socket.cpp`：最小 TCP E2E（PING/SET/GET/DEL）。
- 更新 `docs/testing.md`：增加测试矩阵与执行命令。
- 新增 `docs/decisions.md` 条目（ADR）：模块依赖规则。

验收：

- `ctest --output-on-failure` 全绿。
- E2E 能覆盖半包、粘包、pipeline 基础场景。

## 3.3 Week 3（P1-W1）：TTL 存储与惰性过期

- 修改 `include/db/database.hpp`：扩展 `expire/ttl/pttl/persist` API。
- 修改 `src/db/database.cpp`：引入 `expires_` 结构与惰性删除逻辑。
- 修改 `src/command/commandDispatcher.cpp`：新增命令分发与参数校验：
  - `EXPIRE key seconds`
  - `TTL key`
  - `PTTL key`
  - `PERSIST key`
- 新增 `test/test_ttl.cpp`：TTL 行为测试（存在/不存在/过期边界）。

验收：

- TTL 语义对齐 Redis 常见行为：
  - key 不存在返回 `-2`
  - key 存在但无过期返回 `-1`
  - 过期后读取不可见

## 3.4 Week 4（P1-W2）：TTL 稳定化与文档化

- 扩展 `test/test_e2e_socket.cpp`：TTL 端到端场景。
- 增加异常输入测试：非法过期时间、参数个数错误。
- 更新 `docs/protocol-notes.md`：新增 TTL 相关命令说明。
- 更新 `docs/decisions.md`：补 ADR（对象生命周期 + TTL 删除策略）。

验收：

- 全量测试绿灯。
- 工业化文档与实现一致，无“文档说有、代码没有”。

## 4. 每周节奏建议

- 周一：拆任务 + 先写测试。
- 周二到周四：实现 + 回归。
- 周五：压测、补文档、清技术债。

## 5. 风险控制

- 每次 PR 只做一个小目标，避免混合重构与新功能。
- 任何语义变化都先补测试再改实现。
- 先保证兼容，再做性能优化。

## 6. 完成定义（DoD）

- 代码：功能可运行，接口边界清晰。
- 测试：单测 + 集成 + E2E 覆盖目标场景。
- 文档：`industrial-plan`、`testing`、`decisions` 同步更新。
- 可复现：新同学可按文档在一小时内跑通全链路。
