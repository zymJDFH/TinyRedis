#pragma once

#include "../core/dict.hpp"
#include "../object/redisObject.hpp"
#include <string>

class InMemoryDB {
public:
    InMemoryDB() = default;
    ~InMemoryDB();

    void set(const std::string& key, const std::string& value);
    bool get(const std::string& key, std::string& value);
    int del(const std::string& key);
    bool exists(const std::string& key);
    bool incr(const std::string& key, long long& newValue, std::string& err);

private:
    // 旧实现（便于对比）：
    // std::unordered_map<std::string, std::string> kv_;
    //
    // 第一阶段：value 为 std::string*（已完成）
    // 第二阶段：value 升级为 RedisObject*（当前）
    DICT kv_;
};
