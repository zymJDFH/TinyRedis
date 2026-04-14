#include "../../include/core/dict.hpp"

#include <cstring>
#include <functional>
#include <string>
#include <utility>

namespace {
size_t hashkey(const SDS& key) {
    std::hash<std::string> hasher;
    return hasher(key.c_str());
}

bool keyEquals(const SDS& lhs, const SDS& rhs) {
    return std::strcmp(lhs.c_str(), rhs.c_str()) == 0;
}

size_t roundToPowerOfTwo(size_t n) {
    size_t out = 4;
    while (out < n) {
        out <<= 1;
    }
    return out;
}
} // namespace

bool DICT::isRehashing() const {
    return dict_.rehashidx != -1;
}

void DICT::initTable(dictht& ht, size_t size) {
    // 桶数量统一为 2 的幂，便于用按位与计算下标
    size = roundToPowerOfTwo(size);
    ht.table = new dictEntry*[size]();
    ht.size = size;
    ht.sizemask = size - 1;
    ht.used = 0;
}

void DICT::freeTable(dictht& ht) {
    if (ht.table == nullptr) {
        return;
    }

    for (size_t i = 0; i < ht.size; ++i) {
        dictEntry* entry = ht.table[i];
        while (entry != nullptr) {
            dictEntry* next = entry->next;
            delete entry;
            entry = next;
        }
    }

    delete[] ht.table;
    ht.table = nullptr;
    ht.size = 0;
    ht.sizemask = 0;
    ht.used = 0;
}

dictEntry* DICT::findEntryInTable(const dictht& ht, const SDS& key) const {
    if (ht.table == nullptr || ht.size == 0) {
        return nullptr;
    }

    const size_t index = hashkey(key) & ht.sizemask;
    dictEntry* entry = ht.table[index];
    while (entry != nullptr) {
        if (keyEquals(entry->key, key)) {
            return entry;
        }
        entry = entry->next;
    }
    return nullptr;
}

bool DICT::eraseFromTable(dictht& ht, const SDS& key) {
    if (ht.table == nullptr || ht.size == 0) {
        return false;
    }

    const size_t index = hashkey(key) & ht.sizemask;
    dictEntry* entry = ht.table[index];
    dictEntry* prev = nullptr;

    while (entry != nullptr) {
        if (keyEquals(entry->key, key)) {
            if (prev != nullptr) {
                prev->next = entry->next;
            } else {
                ht.table[index] = entry->next;
            }
            delete entry;
            ht.used--;
            return true;
        }

        prev = entry;
        entry = entry->next;
    }

    return false;
}

void DICT::insertEntryToTable(dictht& ht, dictEntry* entry, size_t hash) {
    const size_t index = hash & ht.sizemask;
    entry->next = ht.table[index];
    ht.table[index] = entry;
    ht.used++;
}

void DICT::finishRehash() {
    // 旧表节点都迁移完后，只需要释放旧桶数组并切换主表
    delete[] dict_.ht[0].table;

    dict_.ht[0] = dict_.ht[1];
    dict_.ht[1].table = nullptr;
    dict_.ht[1].size = 0;
    dict_.ht[1].sizemask = 0;
    dict_.ht[1].used = 0;

    dict_.rehashidx = -1;
}

void DICT::clear() {
    freeTable(dict_.ht[0]);
    freeTable(dict_.ht[1]);
    dict_.rehashidx = -1;
}

DICT::DICT() {
    dict_.rehashidx = -1;

    initTable(dict_.ht[0], 8);
    dict_.ht[1].table = nullptr;
    dict_.ht[1].size = 0;
    dict_.ht[1].sizemask = 0;
    dict_.ht[1].used = 0;
}

DICT::~DICT() {
    clear();
}

DICT::DICT(DICT&& other) noexcept {
    dict_ = other.dict_;

    other.dict_.ht[0].table = nullptr;
    other.dict_.ht[1].table = nullptr;
    other.dict_.ht[0].size = other.dict_.ht[0].used = other.dict_.ht[0].sizemask = 0;
    other.dict_.ht[1].size = other.dict_.ht[1].used = other.dict_.ht[1].sizemask = 0;
    other.dict_.rehashidx = -1;
}

DICT& DICT::operator=(DICT&& other) noexcept {
    if (this != &other) {
        clear();
        dict_ = other.dict_;

        other.dict_.ht[0].table = nullptr;
        other.dict_.ht[1].table = nullptr;
        other.dict_.ht[0].size = other.dict_.ht[0].used = other.dict_.ht[0].sizemask = 0;
        other.dict_.ht[1].size = other.dict_.ht[1].used = other.dict_.ht[1].sizemask = 0;
        other.dict_.rehashidx = -1;
    }
    return *this;
}

bool DICT::set(SDS&& key, void* value) {
    // 写操作推进一步 rehash，降低单次扩容抖动
    if (isRehashing()) {
        rehashStep();
    }

    expandIfNeeded();

    if (dictEntry* entry = findEntryInTable(dict_.ht[0], key)) {
        entry->value = value;
        return true;
    }
    if (isRehashing()) {
        if (dictEntry* entry = findEntryInTable(dict_.ht[1], key)) {
            entry->value = value;
            return true;
        }
    }

    const size_t hash = hashkey(key);
    dictEntry* newEntry = new dictEntry;
    newEntry->key = std::move(key);
    newEntry->value = value;

    // rehash 期间新写入只进 ht[1]
    dictht& target = isRehashing() ? dict_.ht[1] : dict_.ht[0];
    insertEntryToTable(target, newEntry, hash);

    return true;
}

void* DICT::get(const SDS& key) {
    // 读操作同样推进一步，确保重哈希会持续完成
    if (isRehashing()) {
        rehashStep();
    }

    if (dictEntry* entry = findEntryInTable(dict_.ht[0], key)) {
        return entry->value;
    }
    if (isRehashing()) {
        if (dictEntry* entry = findEntryInTable(dict_.ht[1], key)) {
            return entry->value;
        }
    }

    return nullptr;
}

bool DICT::erase(const SDS& key) {
    if (isRehashing()) {
        rehashStep();
    }

    if (eraseFromTable(dict_.ht[0], key)) {
        if (isRehashing() && dict_.ht[0].used == 0) {
            finishRehash();
        }
        return true;
    }

    if (isRehashing() && eraseFromTable(dict_.ht[1], key)) {
        return true;
    }

    return false;
}

size_t DICT::size() {
    // 渐进 rehash 阶段元素会分布在两张表
    return dict_.ht[0].used + dict_.ht[1].used;
}

void DICT::forEach(const std::function<void(const SDS&, void*)>& fn) const {
    if (!fn) {
        return;
    }

    for (int t = 0; t < 2; ++t) {
        const dictht& ht = dict_.ht[t];
        if (ht.table == nullptr) {
            continue;
        }
        for (size_t i = 0; i < ht.size; ++i) {
            dictEntry* entry = ht.table[i];
            while (entry != nullptr) {
                fn(entry->key, entry->value);
                entry = entry->next;
            }
        }
    }
}

void DICT::rehashStep() {
    if (!isRehashing()) {
        return;
    }

    if (dict_.ht[0].used == 0) {
        finishRehash();
        return;
    }
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               
    // 跳过空桶，定位下一个待迁移桶
    while (dict_.rehashidx < static_cast<long>(dict_.ht[0].size) &&
           dict_.ht[0].table[dict_.rehashidx] == nullptr) {
        dict_.rehashidx++;
    }

    if (dict_.rehashidx >= static_cast<long>(dict_.ht[0].size)) {
        finishRehash();
        return;
    }

    dictEntry* entry = dict_.ht[0].table[dict_.rehashidx];
    dict_.ht[0].table[dict_.rehashidx] = nullptr;

    // 迁移当前桶整条链到 ht[1]
    while (entry != nullptr) {
        dictEntry* next = entry->next;
        const size_t hash = hashkey(entry->key);
        insertEntryToTable(dict_.ht[1], entry, hash);
        dict_.ht[0].used--;
        entry = next;
    }

    dict_.rehashidx++;

    if (dict_.ht[0].used == 0) {
        finishRehash();
    }
}

void DICT::expandIfNeeded() {
    if (isRehashing()) {
        return;
    }

    if (dict_.ht[0].table == nullptr || dict_.ht[0].size == 0) {
        initTable(dict_.ht[0], 8);
        return;
    }

    // 负载因子 >= 1 时扩容，并开启渐进 rehash
    if (dict_.ht[0].used >= dict_.ht[0].size) {
        initTable(dict_.ht[1], dict_.ht[0].size * 2);
        dict_.rehashidx = 0;
    }
}
