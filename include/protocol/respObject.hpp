#pragma once

#include <string>
#include <vector>
enum class RESPType
{
    SIMPLE_STRING,
    ERROR,
    INTEGER,
    BULK_STRING,
    ARRAY,
    NULL_BULK,
    NONE
};

class RESPObject
{
public:

    RESPType type;

    std::string str;
    long long integer;

    std::vector<RESPObject> elements;

public:

    RESPObject()
        : type(RESPType::NONE),
          integer(0)
    {}
};
