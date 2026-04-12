#pragma once

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string_view>
//字节对齐
#pragma pack(push,1)

struct SdsHdr8 {
    uint8_t len;    //已用
    uint8_t alloc;      //总容量
    uint8_t flags;      //标记当前头部类型
    char buf[];
};

struct SdsHdr16 {
    uint16_t len;
    uint16_t alloc;
    uint8_t flags;
    char buf[];
};

struct SdsHdr32 {
    uint32_t len;
    uint32_t alloc;
    uint8_t flags;
    char buf[];
};

struct SdsHdr64 {
    uint64_t len;
    uint64_t alloc;
    uint8_t flags;
    char buf[];
};

#pragma pack(pop)

class SDS final
{
public:

    static constexpr uint8_t TYPE_8  = 1;
    static constexpr uint8_t TYPE_16 = 2;
    static constexpr uint8_t TYPE_32 = 3;
    static constexpr uint8_t TYPE_64 = 4;
    static constexpr uint8_t TYPE_MASK = 0x07;

    static constexpr size_t MAX_PREALLOC = 1024 * 1024;

public:

    SDS();
    SDS(const char* str);
    SDS(std::string_view str);

    ~SDS();

    SDS(const SDS&) = delete;
    SDS& operator=(const SDS&) = delete;

    SDS(SDS&& other) noexcept;
    SDS& operator=(SDS&& other) noexcept;

public:

    size_t len() const;
    size_t capacity() const;
    size_t avail() const;

    const char* c_str() const;

    void append(const char* str, size_t len);
    void append(std::string_view str);

    void clear();

private:

    char* buf_;

private:

    static uint8_t select_type(size_t len);
    static size_t hdr_size(uint8_t type);

    void init(const char* str, size_t len);
    void destroy();

    void makeRoomFor(size_t addlen);

    void setLen(size_t len);

};
