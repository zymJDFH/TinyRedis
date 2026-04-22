# TinyRedis 设计说明

本文档描述 TinyRedis 当前版本已经实现的功能、模块边界、命令行为和持久化设计。项目目标是以 C++17 复现一个可运行、可测试、可持续演进的 Redis 核心子集，当前优先级是正确性和模块清晰度。

## 1. 当前实现概览

当前版本已经实现：

- 网络服务：单线程 `epoll` LT 事件循环，监听 `127.0.0.1`，默认端口 `6379`，支持启动参数指定端口。
- 协议层：RESP2 基础解析与编码，支持半包、粘包和连续多条消息解析。
- 命令层：命令解析、参数校验、大小写不敏感分发、Redis 风格错误响应。
- 数据层：String 类型 KV 存储，支持基础读写、批量读写、整数自增自减。
- 过期机制：TTL 元信息、惰性过期、事件循环中周期触发的主动过期。
- 持久化：AOF 追加写入、启动 replay、同步 rewrite。
- 测试：SDS、DICT、RESP、Command、AOF 和 TCP E2E 测试均接入 CTest。

当前尚未实现：

- List / Hash / Set / ZSet 等复合数据类型。
- Redis 配置系统、数据库编号、多客户端事务、复制、RDB、集群。
- `SET EX/PX/NX/XX` 等命令选项。
- 真正后台执行的 `BGREWRITEAOF`。

## 2. 模块划分

```text
Client
  |
  v
net            EpollServer / ClientSession
  |
  v
protocol       RESPParser / RESPEncoder / RESPObject
  |
  v
command        CommandParser / CommandDispatcher
  |
  v
storage        InMemoryDB / RedisObject
  |
  v
core           SDS / DICT

sidecar        AOF / cron
```

核心模块职责：

| 模块 | 主要文件 | 职责 |
| --- | --- | --- |
| `core` | `sds.hpp/.cpp`, `dict.hpp/.cpp` | SDS 字符串和哈希表基础结构 |
| `object` | `redisObject.hpp/.cpp` | Redis 对象模型，目前只有 String + RAW 编码 |
| `protocol` | `respParser.hpp/.cpp`, `respEncoder.hpp/.cpp` | RESP2 请求解析与响应编码 |
| `command` | `commandParser.hpp/.cpp`, `commandDispatcher.hpp/.cpp` | RESP 对象转 argv、命令分发、参数校验、AOF 调用 |
| `storage` | `inMemoryDB.hpp/.cpp` | KV 数据、TTL 元信息、快照导出 |
| `persistentence` | `aof.hpp/.cpp` | AOF 追加、回放、重写 |
| `net` | `epollServer.hpp/.cpp` | TCP 监听、非阻塞 IO、epoll 事件循环、客户端会话 |
| `test` | `test_*.cpp` | 单元测试和端到端测试 |

## 3. 请求处理链路

正常请求链路：

```text
Client
-> EpollServer::handleClientRead
-> RESPParser::feed / parse
-> CommandParser::toArgv
-> CommandDispatcher::dispatch
-> CommandDispatcher::dispatchInternal
-> InMemoryDB
-> RESPEncoder
-> ClientSession::writeBuf
-> EpollServer::handleClientWrite
-> Client
```

`ClientSession` 当前只保存每个连接必须的协议和写状态：

```text
ClientSession
├── RESPParser parser
├── std::string writeBuf
└── bool closeAfterWrite
```

设计要点：

- 网络层使用非阻塞 socket，读事件中循环 `recv`，写事件中尽量清空 `writeBuf`。
- 读缓冲由 `RESPParser` 内部维护，用于处理半包和粘包。
- 请求必须是 RESP Array，数组元素必须是 Bulk String 或 Simple String。
- 命令名在分发时转大写，因此命令大小写不敏感。
- 协议解析错误会返回 `-ERR protocol error: ...`，并在严重解析异常后关闭连接。

## 4. 支持命令

### 4.1 基础命令

| 命令 | 参数 | 返回 | 说明 |
| --- | --- | --- | --- |
| `PING` | 无 | `+PONG` | 健康检查 |
| `PING message` | 1 个参数 | Bulk String | 回显 `message` |

### 4.2 String / KV 命令

| 命令 | 参数 | 返回 | 说明 |
| --- | --- | --- | --- |
| `SET key value` | 2 个参数 | `+OK` | 设置 String 值；覆盖旧值并清理该 key 的 TTL |
| `MSET key value [key value ...]` | 偶数个 key/value 参数 | `+OK` | 批量设置；每个被设置的 key 都会清理 TTL |
| `GET key` | 1 个参数 | Bulk String / Null Bulk | key 不存在或已过期时返回 Null Bulk |
| `MGET key [key ...]` | 至少 1 个 key | Array | 按输入顺序返回多个值，不存在位置返回 Null Bulk |

### 4.3 Key 命令

| 命令 | 参数 | 返回 | 说明 |
| --- | --- | --- | --- |
| `DEL key [key ...]` | 至少 1 个 key | Integer | 返回实际删除数量 |
| `EXISTS key [key ...]` | 至少 1 个 key | Integer | 返回存在数量；重复 key 会按请求次数计数 |

### 4.4 整数命令

| 命令 | 参数 | 返回 | 说明 |
| --- | --- | --- | --- |
| `INCR key` | 1 个参数 | Integer | key 不存在时从 0 开始加 1 |
| `DECR key` | 1 个参数 | Integer | key 不存在时从 0 开始减 1 |
| `INCRBY key increment` | 2 个参数 | Integer | `increment` 必须是完整 int64 字符串 |

整数命令错误规则：

- 当前值不是合法整数时返回 `ERR value is not an integer or out of range`。
- 加减后发生 int64 溢出时返回 `ERR increment or decrement would overflow`。
- 失败的整数写命令不会追加 AOF。

### 4.5 TTL 命令

| 命令 | 参数 | 返回 | 说明 |
| --- | --- | --- | --- |
| `EXPIRE key seconds` | 2 个参数 | Integer | key 存在返回 1，不存在返回 0；`seconds <= 0` 会立即删除 key |
| `TTL key` | 1 个参数 | Integer | `-2` 不存在，`-1` 永不过期，`>=0` 为剩余秒数 |
| `PTTL key` | 1 个参数 | Integer | `-2` 不存在，`-1` 永不过期，`>=0` 为剩余毫秒数 |
| `PERSIST key` | 1 个参数 | Integer | 移除 TTL 成功返回 1；key 不存在或没有 TTL 返回 0 |

### 4.6 AOF 命令

| 命令 | 参数 | 返回 | 说明 |
| --- | --- | --- | --- |
| `REWRITEAOF` | 无 | `+OK` / Error | 基于当前内存快照同步重写 AOF |
| `BGREWRITEAOF` | 无 | `+OK` / Error | 当前实现与 `REWRITEAOF` 相同，仍是同步执行 |

## 5. 数据模型与过期策略

当前 DB 只保存 String 对象：

```text
InMemoryDB
├── DICT kv_       key -> RedisObject*
└── DICT expires_  key -> int64_t* expireAtMs
```

`RedisObject` 当前只有：

```text
type     = STRING
encoding = RAW
ptr      = std::string*
```

过期策略：

- 惰性过期：`GET/DEL/EXISTS/INCR/EXPIRE/TTL/PTTL/PERSIST` 等访问 key 的路径都会先检查该 key 是否过期。
- 主动过期：`EpollServer::run` 每约 100ms 调用一次 `CommandDispatcher::cron`，内部抽样执行 `InMemoryDB::activeExpireCycle(64)`。
- `SET` 和 `MSET` 遵循当前项目的 Redis 默认语义：覆盖值时清理该 key 的旧 TTL。
- `snapshot()` 会先触发一次主动过期扫描，再导出当前仍有效的 String 数据。

## 6. AOF 设计

网络服务中的 `CommandDispatcher` 默认开启 AOF，默认文件为运行目录下的 `appendonly.aof`。AOF 文件不存在时，启动恢复按空库处理。

AOF 当前触发方式：

- 自动追加：网络服务执行写命令成功后，会自动追加到 AOF。
- 自动恢复：服务启动时会自动调用 `loadAof` 回放已有 AOF。
- 手动重写：`REWRITEAOF` / `BGREWRITEAOF` 需要客户端显式发送命令触发；当前两者都是同步重写。

运行时写入：

```text
写命令
-> dispatchInternal(argv, false)
-> InMemoryDB
-> AOF::appendCommand
```

写入规则：

- 参与 AOF 的写命令：`SET/MSET/DEL/INCR/INCRBY/DECR/EXPIRE/PERSIST`。
- 命令参数按 RESP Array 编码落盘，复用协议层编码格式。
- 当前实现每条写命令追加后同步 `fsync`。
- 当前命令链路先修改内存 DB，再追加 AOF；如果追加失败，会向客户端返回 AOF 错误，内存状态不回滚。

启动恢复：

```text
EpollServer::init
-> CommandDispatcher::loadAof
-> AOF::replay
-> dispatchInternal(argv, true)
-> InMemoryDB
```

`dispatchInternal(argv, true)` 表示当前在回放 AOF，不会再次追加 AOF，避免重启后重复写入。

同步重写：

```text
REWRITEAOF / BGREWRITEAOF
-> InMemoryDB::snapshot
-> 生成 SET / EXPIRE 恢复命令
-> AOF::rewriteCommands
```

重写规则：

- `REWRITEAOF` 与 `BGREWRITEAOF` 当前都走同步重写路径。
- 重写基于 `InMemoryDB::snapshot` 生成恢复命令，只保留当前仍有效的 key。
- 每个 key 先生成 `SET key value`；如果 key 有剩余 TTL，再追加 `EXPIRE key seconds`。
- `AOF::rewriteCommands` 先写入 `appendonly.aof.tmp`，成功 `fsync` 后再原子替换目标文件。

## 7. RESP 支持范围

`RESPParser` 当前支持：

- Simple String：`+OK\r\n`
- Error：`-ERR ...\r\n`
- Integer：`:1\r\n`
- Bulk String：`$3\r\nGET\r\n`
- Null Bulk String：`$-1\r\n`
- Array：`*2\r\n$3\r\nGET\r\n$1\r\nk\r\n`

限制：

- 命令请求必须最终转换成 argv 数组。
- Null Array 当前会被视为非法输入。
- 协议层面支持 Simple String 元素，但客户端正常交互建议使用 RESP Array + Bulk String。

## 8. 测试基线

CTest 当前包含：

| 测试 | 覆盖内容 |
| --- | --- |
| `SDSTests` | SDS 创建、追加、扩容、移动语义 |
| `DictTests` | DICT set/get/erase、rehash、遍历 |
| `RespTests` | RESP 编解码、半包、粘包、非法输入 |
| `CommandTests` | 命令解析、命令语义、TTL、错误路径 |
| `AOFTests` | AOF 缺失文件恢复、追加恢复、失败命令不落盘、rewrite、TTL 保留 |
| `E2ETests` | 启动真实 TCP 服务后执行基础命令、批量命令、TTL 流程 |

运行方式：

```bash
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

## 9. 后续演进方向

近期可以优先推进：

- 增加 AOF 配置项：是否启用、文件路径、`fsync` 策略。
- 将 `BGREWRITEAOF` 改为真正后台重写。
- 补齐 String 常用命令选项，如 `SET EX/PX/NX/XX`。
- 增加配置文件或启动参数，避免 AOF 文件固定在运行目录。
- 扩展 List / Hash / Set / ZSet 数据类型。
