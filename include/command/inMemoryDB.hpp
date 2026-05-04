#pragma once

#include "../core/dict.hpp"
#include "../object/redisObject.hpp"
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

//DB层的统一返回状态，告诉上层命令分发器这次访问的结果是什么
enum class DBStatus {
    Ok = 0,
    NotFound,
    WrongType,
};

//Hash中的字段
struct DBHashFieldEntry {
    std::string field;
    std::string value;
};

//DB中一个key的快照表示
struct DBSnapshotEntry {
    RedisObjectType type;
    std::string key;
    long long ttlMs;
    std::string stringValue;
    std::vector<DBHashFieldEntry> hashEntries;
};

class InMemoryDB {
public:
    InMemoryDB() = default;
    ~InMemoryDB();

    // SET key value；会覆盖旧值并清理该 key 的 TTL。
    void set(const std::string& key, const std::string& value);
    // GET key；返回 false 表示 key 不存在（或已过期）。
    bool get(const std::string& key, std::string& value);
    // GET key 的带类型版本；可区分不存在和类型错误。
    DBStatus getStringValue(const std::string& key, std::string& value);
    // DEL key；返回 1 表示删除成功，0 表示 key 不存在。
    int del(const std::string& key);
    // EXISTS key；过期 key 视为不存在。
    bool exists(const std::string& key);
    // INCR key；返回 false 时 err 带错误信息。
    bool incr(const std::string& key, long long& newValue, std::string& err);
    // INCRBY/DECR 的通用实现；delta 可正可负。
    bool incrBy(const std::string& key, long long delta, long long& newValue, std::string& err);
    // HSET key field value [field value ...]；返回本次新增 field 的数量。
    DBStatus hset(const std::string& key,
                  const std::vector<std::pair<std::string, std::string>>& fieldValues,
                  int& addedCount);
    // HGET key field；NotFound 表示 key 或 field 不存在。
    DBStatus hget(const std::string& key, const std::string& field, std::string& value);
    // HDEL key field [field ...]；返回删除的 field 数量。
    DBStatus hdel(const std::string& key, const std::vector<std::string>& fields, int& removedCount);
    // HEXISTS key field；NotFound 表示 key 或 field 不存在。
    DBStatus hexists(const std::string& key, const std::string& field, bool& exists);
    // HLEN key；NotFound 表示 key 不存在。
    DBStatus hlen(const std::string& key, size_t& len);
    // HGETALL/HKEYS/HVALS 的底层导出接口；NotFound 表示 key 不存在。
    DBStatus hgetall(const std::string& key, std::vector<DBHashFieldEntry>& entries);

    // EXPIRE key seconds；返回 1 表示设置成功，0 表示 key 不存在。
    int expire(const std::string& key, long long ttlSeconds);
    // TTL key；-2 不存在，-1 存在但无过期时间，>=0 剩余秒数。
    long long ttl(const std::string& key);
    // PTTL key；-2 不存在，-1 存在但无过期时间，>=0 剩余毫秒数。
    long long pttl(const std::string& key);
    // PERSIST key；返回 1 表示移除过期时间成功，0 表示无变化。
    int persist(const std::string& key);
    // 主动过期扫描；最多检查 sampleCount 个 TTL 条目，返回本轮删除数量。
    size_t activeExpireCycle(size_t sampleCount);
    // 导出当前有效 String 数据；ttlMs 为 -1 表示无过期时间。
    std::vector<DBSnapshotEntry> snapshot();

private:
    RedisObject* getObject(const std::string& key);
    // 惰性过期检查：若 key 已过期则立即删除并返回 true。
    bool expireIfNeeded(const std::string& key);
    // 获取当前单调时钟毫秒时间戳。
    static int64_t nowMs();
    // 设置 key 的绝对过期时间（毫秒）；会覆盖旧值。
    void setExpireAtMs(const std::string& key, int64_t expireAtMs);
    // 获取 key 的绝对过期时间（毫秒）；不存在返回 false。
    bool getExpireAtMs(const std::string& key, int64_t& expireAtMs);
    // 删除 key 的过期时间；存在则释放内存并返回 true。
    bool eraseExpire(const std::string& key);

    DICT kv_;
    // key -> int64_t*（绝对过期时间戳，毫秒）
    DICT expires_;
};
