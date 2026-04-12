#pragma once
#include <string>
#include <vector>

class RESPEncoder
{
public:

    static std::string simpleString(const std::string&);

    static std::string error(const std::string&);

    static std::string integer(long long);

    static std::string bulkString(const std::string&);

    static std::string nullBulk();

    static std::string array(const std::vector<std::string>&);
};