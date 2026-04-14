#include "../../include/object/redisObject.hpp"

RedisObject* createStringObject(const std::string& value) {
    RedisObject* obj = new RedisObject;
    obj->type = RedisObjectType::STRING;
    obj->encoding = RedisObjectEncoding::RAW;
    obj->ptr = new std::string(value);
    return obj;
}

void freeRedisObject(RedisObject* obj) {
    if (obj == nullptr) {
        return;
    }

    if (obj->type == RedisObjectType::STRING) {
        delete static_cast<std::string*>(obj->ptr);
        obj->ptr = nullptr;
    }

    delete obj;
}

const std::string* getStringObjectValue(const RedisObject* obj) {
    if (obj == nullptr || obj->type != RedisObjectType::STRING) {
        return nullptr;
    }
    return static_cast<const std::string*>(obj->ptr);
}
