# RESP 协议笔记

## 当前支持类型

- `+` Simple String
- `-` Error
- `:` Integer
- `$` Bulk String（包含 null bulk）
- `*` Array

## Parser 接口约定

- `feed(data, len)`：将字节流追加到内部缓冲区。
- `parse(out)`:
  - 解析出完整对象时返回 `true`
  - 数据不完整时返回 `false`
  - 协议格式错误时可抛异常

## 命令输入形态

Redis 命令请求通常是“由 Bulk String 组成的 RESP Array”。

示例：

```
*2\r\n$4\r\nPING\r\n$4\r\nPONG\r\n
```

目标转换：

- `RESPObject(ARRAY)` -> `std::vector<std::string> argv`

规则：

- 命令名是 `argv[0]`（大小写不敏感）。
- 参数是 `argv[1...]`。
- 非 array 或包含非 bulk 参数时，应返回协议错误回复。

## 错误处理策略（计划）

- 协议解析错误 -> `-ERR Protocol error\r\n`
- 未知命令 -> `-ERR unknown command '<cmd>'\r\n`
- 参数个数错误 -> `-ERR wrong number of arguments for '<cmd>'\r\n`

## 当前已实现命令

- `PING`
- `SET key value`
- `GET key`
- `DEL key [key ...]`
- `EXISTS key`
- `INCR key`
