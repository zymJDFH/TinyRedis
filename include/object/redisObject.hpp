#pragma once

#include <string>

enum class RedisObjectType {
    STRING = 0,
};

enum class RedisObjectEncoding {
    RAW = 0,
};

struct RedisObject {
    RedisObjectType type;
    RedisObjectEncoding encoding;
    void* ptr;
};

// 创建一个 string 类型对象（内部持有 new std::string）
RedisObject* createStringObject(const std::string& value);

// 释放对象及其内部资源
void freeRedisObject(RedisObject* obj);

// 读取 string 对象中的值；若类型不匹配返回 nullptr
const std::string* getStringObjectValue(const RedisObject* obj);
