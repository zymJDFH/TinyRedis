#include "include/core/sds.hpp"
#include "include/core/dict.hpp"
#include <iostream>

int main()
{
    // 测试 SDS
    SDS s("hello");
    s.append(" world", 6);
    std::cout << "SDS: " << s.c_str() << std::endl;
    std::cout << "len=" << s.len() << ", cap=" << s.capacity() << std::endl;

    // 测试 DICT
    std::cout << "\n=== DICT Test ===" << std::endl;

    DICT dict;
    int val1 = 100, val2 = 200;

    // set & get
    dict.set(SDS("name"), &val1);
    std::cout << "set name=100, size=" << dict.size() << std::endl;

    int* got = static_cast<int*>(dict.get(SDS("name")));
    if (got) {
        std::cout << "get name=" << *got << std::endl;
    } else {
        std::cout << "get name=nullptr" << std::endl;
    }

    // update
    dict.set(SDS("name"), &val2);
    got = static_cast<int*>(dict.get(SDS("name")));
    std::cout << "update name=200, get name=" << (got ? *got : -1) << ", size=" << dict.size() << std::endl;

    // erase
    bool erased = dict.erase(SDS("name"));
    std::cout << "erase name=" << (erased ? "true" : "false") << ", size=" << dict.size() << std::endl;

    // get after erase
    got = static_cast<int*>(dict.get(SDS("name")));
    std::cout << "get after erase=" << (got ? std::to_string(*got) : "nullptr") << std::endl;

    // multiple keys
    int v1 = 10, v2 = 20, v3 = 30;
    dict.set(SDS("a"), &v1);
    dict.set(SDS("b"), &v2);
    dict.set(SDS("c"), &v3);
    std::cout << "\nMultiple keys, size=" << dict.size() << std::endl;

    return 0;
}