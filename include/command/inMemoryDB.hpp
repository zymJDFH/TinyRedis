#pragma once

#include "../core/dict.hpp"
#include "../object/redisObject.hpp"
#include <cstddef>
#include <cstdint>
#include <string>

class InMemoryDB {
public:
    InMemoryDB() = default;
    ~InMemoryDB();

    // SET key value；会覆盖旧值并清理该 key 的 TTL。
    void set(const std::string& key, const std::string& value);
    // GET key；返回 false 表示 key 不存在（或已过期）。
    bool get(const std::string& key, std::string& value);
    // DEL key；返回 1 表示删除成功，0 表示 key 不存在。
    int del(const std::string& key);
    // EXISTS key；过期 key 视为不存在。
    bool exists(const std::string& key);
    // INCR key；返回 false 时 err 带错误信息。
    bool incr(const std::string& key, long long& newValue, std::string& err);

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

private:
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
