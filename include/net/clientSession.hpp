#pragma once

#include "../protocol/respParser.hpp"

#include <string>

struct ClientSession {
    RESPParser parser;
    std::string writeBuf;
    bool closeAfterWrite = false;
    bool replica = false;
};
