#pragma once
#include "respObject.hpp"
#include <string>

class RESPParser
{
public:

    RESPParser();

    // 喂数据
    void feed(const char* data, size_t len);

    // 尝试解析一个完整RESP
    bool parse(RESPObject& out);

private:

    std::string buffer_;
    size_t pos_;

private:

    bool parseInternal(RESPObject&);

    bool parseSimpleString(RESPObject&);
    bool parseError(RESPObject&);
    bool parseInteger(RESPObject&);
    bool parseBulkString(RESPObject&);
    bool parseArray(RESPObject&);

    bool readLine(std::string& line);
};
