#include "../../include/protocol/respEncoder.hpp"

std::string RESPEncoder::simpleString(const std::string& s) {
    return "+" + s + "\r\n";
}

std::string RESPEncoder::error(const std::string& s) {
    return "-" + s + "\r\n";
}

std::string RESPEncoder::integer(long long value) {
    return ":" + std::to_string(value) + "\r\n";
}

std::string RESPEncoder::bulkString(const std::string& s) {
    return "$" + std::to_string(s.size()) + "\r\n" + s + "\r\n";
}

std::string RESPEncoder::nullBulk() {
    return "$-1\r\n";
}

std::string RESPEncoder::array(const std::vector<std::string>& elements) {
    std::string out = "*" + std::to_string(elements.size()) + "\r\n";
    for (const auto& e : elements) {
        out += bulkString(e);
    }
    return out;
}
