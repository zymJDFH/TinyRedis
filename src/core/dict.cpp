#include "../../include/core/dict.hpp"

static size_t hashkey(const SDS&key){
    std::hash<std::string>hasher;
    return hasher(key.c_str());
    //可以优化？好像不安全·
    // std::string_view sv(key.data(), key.size());
    // return std::hash<std::string_view>{}(sv);
}

DICT::DICT(){
    dict_.rehashidx=-1;

    dict_.ht[0].size = 8;
    dict_.ht[0].sizemask = 7;
    dict_.ht[0].used = 0;

    dict_.ht[0].table=new dictEntry*[8];
    for(size_t i=0;i<8;i++){
        dict_.ht[0].table[i]=nullptr;
    }
    dict_.ht[1].table = nullptr;
    dict_.ht[1].size = 0;
    dict_.ht[1].used = 0;
}

DICT::~DICT(){
    for(size_t i=0;i<dict_.ht[0].size;i++){
        dictEntry* entry=dict_.ht[0].table[i];
        while(entry){
            dictEntry*next=entry->next;
            delete entry;
            entry=next;
        }
    }
    delete[] dict_.ht[0].table;
    //dict[1]
}

bool DICT::set(SDS&& key, void* value){
    size_t hash=hashkey(key);
    size_t index = hash & dict_.ht[0].sizemask;
    dictEntry* entry = dict_.ht[0].table[index];
    while(entry)
    {
        if(strcmp(entry->key.c_str(), key.c_str()) == 0)
        {
            //这里好像会内存泄漏
            entry->value = value;
            return true;
        }
        entry = entry->next;
    }

    dictEntry* newEntry = new dictEntry;

    newEntry->key = std::move(key);
    newEntry->value = value;

    newEntry->next = dict_.ht[0].table[index];

    dict_.ht[0].table[index] = newEntry;
    dict_.ht[0].used++;
    return true;
}

void* DICT::get(const SDS&key){
    size_t hash = hashkey(key);

    size_t index = hash & dict_.ht[0].sizemask;

    dictEntry* entry = dict_.ht[0].table[index];

    while(entry)
    {
        if(strcmp(entry->key.c_str(), key.c_str()) == 0)
            return entry->value;

        entry = entry->next;
    }

    return nullptr;
}

bool DICT::erase(const SDS&key){
    size_t hash = hashkey(key);

    size_t index = hash & dict_.ht[0].sizemask;

    dictEntry* entry = dict_.ht[0].table[index];
    dictEntry* prev = nullptr;

    while(entry)
    {
        if(strcmp(entry->key.c_str(), key.c_str()) == 0)
        {
            if(prev)
                prev->next = entry->next;
            else
                dict_.ht[0].table[index] = entry->next;

            delete entry;

            dict_.ht[0].used--;

            return true;
        }

        prev = entry;
        entry = entry->next;
    }

    return false;
}

size_t DICT::size(){
    return dict_.ht[0].used;
}

void DICT::rehashStep(){

}
void DICT::expandIfNeeded(){

}