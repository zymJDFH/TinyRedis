#include "../../include/object/redisObject.hpp"

RedisObject* createStringObject(const std::string& value) {
    RedisObject* obj = new RedisObject;
    obj->type = RedisObjectType::STRING;
    obj->encoding = RedisObjectEncoding::RAW;
    obj->ptr = new std::string(value);
    return obj;
}

RedisObject* createHashObject() {
    RedisObject* obj = new RedisObject;
    obj->type = RedisObjectType::HASH;
    obj->encoding = RedisObjectEncoding::RAW;
    obj->ptr = new DICT();
    return obj;
}

void freeRedisObject(RedisObject* obj) {
    if (obj == nullptr) {
        return;
    }

    if (obj->type == RedisObjectType::STRING) {
        delete static_cast<std::string*>(obj->ptr);
        obj->ptr = nullptr;
    } else if (obj->type == RedisObjectType::HASH) {
        DICT* hash = static_cast<DICT*>(obj->ptr);
        if (hash != nullptr) {
            hash->forEach([](const SDS&, void* value) {
                delete static_cast<std::string*>(value);
            });
            delete hash;
        }
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

DICT* getHashObjectValue(RedisObject* obj) {
    if (obj == nullptr || obj->type != RedisObjectType::HASH) {
        return nullptr;
    }
    return static_cast<DICT*>(obj->ptr);
}

const DICT* getHashObjectValue(const RedisObject* obj) {
    if (obj == nullptr || obj->type != RedisObjectType::HASH) {
        return nullptr;
    }
    return static_cast<const DICT*>(obj->ptr);
}
