#pragma once
#include "sds.hpp"
#include <cstddef>

struct dictEntry{
    SDS key;
    void *value;
    dictEntry *next;
};

struct dictht{
    dictEntry **table;
    size_t size;
    size_t sizemask;
    size_t used;
};

struct dict{
    dictht ht[2];
    long rehashidx;
};
class DICT{
public:
    DICT();
    ~DICT();
    DICT(DICT&&other)noexcept;
    DICT& operator=(DICT&&other)noexcept;
    bool set(SDS&& key, void* value);
    void* get(const SDS&key);
    bool erase(const SDS&key);
    size_t size();

private:
    dict dict_;
private:
    // 判断是否处于渐进式 rehash 阶段
    bool isRehashing() const;

    // 初始化/释放哈希表
    void initTable(dictht& ht, size_t size);
    void freeTable(dictht& ht);

    // 在指定表中查找/删除 key
    dictEntry* findEntryInTable(const dictht& ht, const SDS& key) const;
    bool eraseFromTable(dictht& ht, const SDS& key);

    // 把节点按 hash 插入到指定表（头插法）
    void insertEntryToTable(dictht& ht, dictEntry* entry, size_t hash);

    // rehash 完成后把 ht[1] 切换为主表
    void finishRehash();

    void rehashStep();
    void expandIfNeeded();
    void clear();
};
