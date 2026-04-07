#ifndef DICT_HPP
#define DICT_HPP
#include "sds.hpp"

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
    bool set(SDS&& key, void* value);
    void* get(const SDS&key);
    bool erase(const SDS&key);
    size_t size();

private:
    dict dict_;
private:
    void rehashStep();
    void expandIfNeeded();
};

#endif
