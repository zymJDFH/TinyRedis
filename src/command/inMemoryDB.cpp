#include "../../include/command/inMemoryDB.hpp"

#include <limits>
#include <stdexcept>

namespace {
RedisObject* asObject(void* p) {
    return static_cast<RedisObject*>(p);
}
} // namespace

InMemoryDB::~InMemoryDB() {
    // 释放 DICT 中剩余对象，避免内存泄漏
    kv_.forEach([](const SDS&, void* v) {
        freeRedisObject(asObject(v));
    });
}

void InMemoryDB::set(const std::string& key, const std::string& value) {
    // 最早实现：kv_[key] = value;
    // 上一阶段：value 是 std::string*。
    // 当前阶段：value 升级为 RedisObject*（string 对象）。

    void* old = kv_.get(SDS(key));
    RedisObject* newObj = createStringObject(value);
    kv_.set(SDS(key), newObj);

    if (old != nullptr) {
        freeRedisObject(asObject(old));
    }
}

bool InMemoryDB::get(const std::string& key, std::string& value) {
    void* raw = kv_.get(SDS(key));
    if (raw == nullptr) {
        return false;
    }

    const std::string* s = getStringObjectValue(asObject(raw));
    if (s == nullptr) {
        return false;
    }
    value = *s;
    return true;
}

int InMemoryDB::del(const std::string& key) {
    SDS sKey(key);
    void* raw = kv_.get(sKey);
    if (raw == nullptr) {
        return 0;
    }

    if (!kv_.erase(sKey)) {
        return 0;
    }

    freeRedisObject(asObject(raw));
    return 1;
}

bool InMemoryDB::exists(const std::string& key) {
    return kv_.get(SDS(key)) != nullptr;
}

bool InMemoryDB::incr(const std::string& key, long long& newValue, std::string& err) {
    err.clear();
    long long value = 0;

    void* raw = kv_.get(SDS(key));
    if (raw != nullptr) {
        const std::string* s = getStringObjectValue(asObject(raw));
        if (s == nullptr) {
            err = "value is not an integer or out of range";
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

    if (value == std::numeric_limits<long long>::max()) {
        err = "increment or decrement would overflow";
        return false;
    }

    newValue = value + 1;

    RedisObject* newObj = createStringObject(std::to_string(newValue));
    kv_.set(SDS(key), newObj);

    if (raw != nullptr) {
        freeRedisObject(asObject(raw));
    }

    return true;
}
