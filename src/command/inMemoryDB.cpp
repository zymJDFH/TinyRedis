#include "../../include/command/inMemoryDB.hpp"

#include <algorithm>
#include <chrono>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
RedisObject* asObject(void* p) {
    return static_cast<RedisObject*>(p);
}

std::string* asStringValue(void* p) {
    return static_cast<std::string*>(p);
}

int64_t* asExpireAt(void* p) {
    return static_cast<int64_t*>(p);
}

bool addWouldOverflow(long long base, long long delta) {
    if (delta > 0 && base > std::numeric_limits<long long>::max() - delta) {
        return true;
    }
    if (delta < 0 && base < std::numeric_limits<long long>::min() - delta) {
        return true;
    }
    return false;
}
} // namespace

int64_t InMemoryDB::nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

RedisObject* InMemoryDB::getObject(const std::string& key) {
    expireIfNeeded(key);
    return asObject(kv_.get(SDS(key)));
}

void InMemoryDB::setExpireAtMs(const std::string& key, int64_t expireAtMs) {
    SDS sKey(key);
    void* old = expires_.get(sKey);
    expires_.set(SDS(key), new int64_t(expireAtMs));
    if (old != nullptr) {
        delete asExpireAt(old);
    }
}

bool InMemoryDB::getExpireAtMs(const std::string& key, int64_t& expireAtMs) {
    void* raw = expires_.get(SDS(key));
    if (raw == nullptr) {
        return false;
    }
    expireAtMs = *asExpireAt(raw);
    return true;
}

bool InMemoryDB::eraseExpire(const std::string& key) {
    SDS sKey(key);
    void* raw = expires_.get(sKey);
    if (raw == nullptr) {
        return false;
    }

    if (!expires_.erase(sKey)) {
        return false;
    }

    delete asExpireAt(raw);
    return true;
}

bool InMemoryDB::expireIfNeeded(const std::string& key) {
    int64_t expireAtMs = 0;
    if (!getExpireAtMs(key, expireAtMs)) {
        return false;
    }

    if (expireAtMs > nowMs()) {
        return false;
    }

    const SDS sKey(key);
    void* raw = kv_.get(sKey);
    if (raw != nullptr && kv_.erase(sKey)) {
        freeRedisObject(asObject(raw));
    }
    (void)eraseExpire(key);
    return true;
}

InMemoryDB::~InMemoryDB() {
    // 释放 DICT 中剩余对象，避免内存泄漏
    kv_.forEach([](const SDS&, void* v) {
        freeRedisObject(asObject(v));
    });
    // 释放 expires_ 中剩余时间戳指针
    expires_.forEach([](const SDS&, void* v) {
        delete asExpireAt(v);
    });
}

void InMemoryDB::set(const std::string& key, const std::string& value) {
    expireIfNeeded(key);

    void* old = kv_.get(SDS(key));
    RedisObject* newObj = createStringObject(value);
    kv_.set(SDS(key), newObj);

    if (old != nullptr) {
        freeRedisObject(asObject(old));
    }

    // Redis 默认行为：SET 会清理已有 TTL（除非显式 KEEPTTL）
    (void)eraseExpire(key);
}

bool InMemoryDB::get(const std::string& key, std::string& value) {
    return getStringValue(key, value) == DBStatus::Ok;
}

DBStatus InMemoryDB::getStringValue(const std::string& key, std::string& value) {
    RedisObject* obj = getObject(key);
    if (obj == nullptr) {
        return DBStatus::NotFound;
    }

    const std::string* s = getStringObjectValue(obj);
    if (s == nullptr) {
        return DBStatus::WrongType;
    }
    value = *s;
    return DBStatus::Ok;
}

int InMemoryDB::del(const std::string& key) {
    if (expireIfNeeded(key)) {
        return 0;
    }

    SDS sKey(key);
    void* raw = kv_.get(sKey);
    if (raw == nullptr) {
        return 0;
    }

    if (!kv_.erase(sKey)) {
        return 0;
    }

    freeRedisObject(asObject(raw));
    (void)eraseExpire(key);
    return 1;
}

bool InMemoryDB::exists(const std::string& key) {
    expireIfNeeded(key);
    return kv_.get(SDS(key)) != nullptr;
}

bool InMemoryDB::incr(const std::string& key, long long& newValue, std::string& err) {
    return incrBy(key, 1, newValue, err);
}

bool InMemoryDB::incrBy(const std::string& key,
                        long long delta,
                        long long& newValue,
                        std::string& err) {
    err.clear();
    long long value = 0;

    RedisObject* obj = getObject(key);
    void* raw = obj;
    if (obj != nullptr) {
        const std::string* s = getStringObjectValue(obj);
        if (s == nullptr) {
            err = "WRONGTYPE Operation against a key holding the wrong kind of value";
            return false;
        }
        try {
            size_t parsed = 0;
            value = std::stoll(*s, &parsed, 10);
            if (parsed != s->size()) {
                err = "value is not an integer or out of range";
                return false;
            }
        } catch (const std::exception&) {
            err = "value is not an integer or out of range";
            return false;
        }
    }

    if (addWouldOverflow(value, delta)) {
        err = "increment or decrement would overflow";
        return false;
    }

    newValue = value + delta;

    RedisObject* newObj = createStringObject(std::to_string(newValue));
    kv_.set(SDS(key), newObj);

    if (raw != nullptr) {
        freeRedisObject(asObject(raw));
    }

    return true;
}

DBStatus InMemoryDB::hset(const std::string& key,
                          const std::vector<std::pair<std::string, std::string>>& fieldValues,
                          int& addedCount) {
    addedCount = 0;
    RedisObject* obj = getObject(key);

    if (obj == nullptr) {
        obj = createHashObject();
        kv_.set(SDS(key), obj);
    } else if (obj->type != RedisObjectType::HASH) {
        return DBStatus::WrongType;
    }

    DICT* hash = getHashObjectValue(obj);
    for (const auto& [field, value] : fieldValues) {
        SDS sField(field);
        void* old = hash->get(sField);
        hash->set(SDS(field), new std::string(value));
        if (old == nullptr) {
            ++addedCount;
        } else {
            delete asStringValue(old);
        }
    }

    return DBStatus::Ok;
}

DBStatus InMemoryDB::hget(const std::string& key, const std::string& field, std::string& value) {
    RedisObject* obj = getObject(key);
    if (obj == nullptr) {
        return DBStatus::NotFound;
    }
    if (obj->type != RedisObjectType::HASH) {
        return DBStatus::WrongType;
    }

    DICT* hash = getHashObjectValue(obj);
    void* raw = hash->get(SDS(field));
    if (raw == nullptr) {
        return DBStatus::NotFound;
    }

    value = *asStringValue(raw);
    return DBStatus::Ok;
}

DBStatus InMemoryDB::hdel(const std::string& key, const std::vector<std::string>& fields, int& removedCount) {
    removedCount = 0;
    RedisObject* obj = getObject(key);
    if (obj == nullptr) {
        return DBStatus::NotFound;
    }
    if (obj->type != RedisObjectType::HASH) {
        return DBStatus::WrongType;
    }

    DICT* hash = getHashObjectValue(obj);
    for (const std::string& field : fields) {
        SDS sField(field);
        void* raw = hash->get(sField);
        if (raw == nullptr) {
            continue;
        }
        if (hash->erase(sField)) {
            delete asStringValue(raw);
            ++removedCount;
        }
    }

    if (hash->size() == 0) {
        const SDS sKey(key);
        void* raw = kv_.get(sKey);
        if (raw != nullptr && kv_.erase(sKey)) {
            freeRedisObject(asObject(raw));
            (void)eraseExpire(key);
        }
    }

    return DBStatus::Ok;
}

DBStatus InMemoryDB::hexists(const std::string& key, const std::string& field, bool& exists) {
    exists = false;
    RedisObject* obj = getObject(key);
    if (obj == nullptr) {
        return DBStatus::NotFound;
    }
    if (obj->type != RedisObjectType::HASH) {
        return DBStatus::WrongType;
    }

    DICT* hash = getHashObjectValue(obj);
    exists = hash->get(SDS(field)) != nullptr;
    return exists ? DBStatus::Ok : DBStatus::NotFound;
}

DBStatus InMemoryDB::hlen(const std::string& key, size_t& len) {
    len = 0;
    RedisObject* obj = getObject(key);
    if (obj == nullptr) {
        return DBStatus::NotFound;
    }
    if (obj->type != RedisObjectType::HASH) {
        return DBStatus::WrongType;
    }

    DICT* hash = getHashObjectValue(obj);
    len = hash->size();
    return DBStatus::Ok;
}

DBStatus InMemoryDB::hgetall(const std::string& key, std::vector<DBHashFieldEntry>& entries) {
    entries.clear();

    RedisObject* obj = getObject(key);
    if (obj == nullptr) {
        return DBStatus::NotFound;
    }
    if (obj->type != RedisObjectType::HASH) {
        return DBStatus::WrongType;
    }

    const DICT* hash = getHashObjectValue(obj);
    hash->forEach([&entries](const SDS& field, void* rawValue) {
        entries.push_back(DBHashFieldEntry {field.c_str(), *asStringValue(rawValue)});
    });

    std::sort(entries.begin(), entries.end(), [](const DBHashFieldEntry& lhs, const DBHashFieldEntry& rhs) {
        return lhs.field < rhs.field;
    });

    return DBStatus::Ok;
}

//给键设置秒级的过期时间
int InMemoryDB::expire(const std::string& key, long long ttlSeconds) {
    expireIfNeeded(key);

    if (kv_.get(SDS(key)) == nullptr) {
        (void)eraseExpire(key);
        return 0;
    }

    if (ttlSeconds <= 0) {
        (void)del(key);
        return 1;
    }

    const int64_t now = nowMs();
    int64_t expireAt = std::numeric_limits<int64_t>::max();
    const int64_t maxDeltaSec = (std::numeric_limits<int64_t>::max() - now) / 1000;
    if (ttlSeconds <= maxDeltaSec) {
        expireAt = now + ttlSeconds * 1000;
    }
    setExpireAtMs(key, expireAt);
    return 1;
}

// 查询键剩余多少毫秒过期
long long InMemoryDB::pttl(const std::string& key) {
    expireIfNeeded(key);

    if (kv_.get(SDS(key)) == nullptr) {
        (void)eraseExpire(key);
        return -2;
    }

    int64_t expireAtMs = 0;
    if (!getExpireAtMs(key, expireAtMs)) {
        return -1;
    }

    const int64_t remainMs = expireAtMs - nowMs();
    if (remainMs <= 0) {
        (void)del(key);
        return -2;
    }
    return remainMs;
}

//查询键剩余多少秒过期
long long InMemoryDB::ttl(const std::string& key) {
    const long long remainMs = pttl(key);
    if (remainMs < 0) {
        return remainMs;
    }
    return remainMs / 1000;
}

//取消键的过期时间
int InMemoryDB::persist(const std::string& key) {
    expireIfNeeded(key);

    if (kv_.get(SDS(key)) == nullptr) {
        (void)eraseExpire(key);
        return 0;
    }

    return eraseExpire(key) ? 1 : 0;
}

size_t InMemoryDB::activeExpireCycle(size_t sampleCount) {
    if (sampleCount == 0 || expires_.size() == 0) {
        return 0;
    }

    size_t checked = 0;
    size_t removed = 0;
    const int64_t now = nowMs();
    std::vector<std::string> expiredKeys;
    expiredKeys.reserve(sampleCount);

    expires_.forEach([&](const SDS& key, void* value) {
        if (checked >= sampleCount) {
            return;
        }
        ++checked;

        if (*asExpireAt(value) <= now) {
            expiredKeys.emplace_back(key.c_str());
        }
    });

    for (const std::string& key : expiredKeys) {
        if (!eraseExpire(key)) {
            continue;
        }

        const SDS sKey(key);
        void* raw = kv_.get(sKey);
        if (raw != nullptr && kv_.erase(sKey)) {
            freeRedisObject(asObject(raw));
            ++removed;
        }
    }

    return removed;
}

std::vector<DBSnapshotEntry> InMemoryDB::snapshot() {
    (void)activeExpireCycle(expires_.size());

    std::vector<DBSnapshotEntry> entries;
    entries.reserve(kv_.size());
    const int64_t now = nowMs();

    kv_.forEach([&](const SDS& key, void* value) {
        const std::string keyStr = key.c_str();
        int64_t expireAtMs = 0;
        long long ttlMs = -1;
        if (getExpireAtMs(keyStr, expireAtMs)) {
            ttlMs = expireAtMs - now;
            if (ttlMs <= 0) {
                return;
            }
        }

        RedisObject* obj = asObject(value);
        DBSnapshotEntry entry {};
        entry.type = obj->type;
        entry.key = keyStr;
        entry.ttlMs = ttlMs;

        if (const std::string* strValue = getStringObjectValue(obj); strValue != nullptr) {
            entry.stringValue = *strValue;
            entries.push_back(std::move(entry));
            return;
        }

        const DICT* hash = getHashObjectValue(obj);
        if (hash == nullptr) {
            return;
        }

        entry.hashEntries.reserve(hash->size());
        hash->forEach([&](const SDS& field, void* fieldValue) {
            entry.hashEntries.push_back(DBHashFieldEntry {field.c_str(), *asStringValue(fieldValue)});
        });
        entries.push_back(std::move(entry));
    });

    return entries;
}
