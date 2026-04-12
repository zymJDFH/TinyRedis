#include "../../include/protocol/respParser.hpp"

#include <stdexcept>

RESPParser::RESPParser()
    : pos_(0) {}

void RESPParser::feed(const char* data, size_t len) {
    if (data == nullptr || len == 0) {
        return;
    }
    buffer_.append(data, len);
}

bool RESPParser::parse(RESPObject& out) {
    const size_t start = pos_;
    if (!parseInternal(out)) {
        pos_ = start;
        return false;
    }

    if (pos_ > 0) {
        buffer_.erase(0, pos_);
        pos_ = 0;
    }
    return true;
}

bool RESPParser::parseInternal(RESPObject& out) {
    if (pos_ >= buffer_.size()) {
        return false;
    }

    const char prefix = buffer_[pos_];
    switch (prefix) {
    case '+':
        return parseSimpleString(out);
    case '-':
        return parseError(out);
    case ':':
        return parseInteger(out);
    case '$':
        return parseBulkString(out);
    case '*':
        return parseArray(out);
    default:
        throw std::runtime_error("invalid RESP type prefix");
    }
}

bool RESPParser::parseSimpleString(RESPObject& out) {
    if (pos_ >= buffer_.size() || buffer_[pos_] != '+') {
        return false;
    }
    ++pos_;

    std::string line;
    if (!readLine(line)) {
        return false;
    }

    out.type = RESPType::SIMPLE_STRING;
    out.str = line;
    out.integer = 0;
    out.elements.clear();
    return true;
}

bool RESPParser::parseError(RESPObject& out) {
    if (pos_ >= buffer_.size() || buffer_[pos_] != '-') {
        return false;
    }
    ++pos_;

    std::string line;
    if (!readLine(line)) {
        return false;
    }

    out.type = RESPType::ERROR;
    out.str = line;
    out.integer = 0;
    out.elements.clear();
    return true;
}

bool RESPParser::parseInteger(RESPObject& out) {
    if (pos_ >= buffer_.size() || buffer_[pos_] != ':') {
        return false;
    }
    ++pos_;

    std::string line;
    if (!readLine(line)) {
        return false;
    }

    out.type = RESPType::INTEGER;
    out.integer = std::stoll(line);
    out.str.clear();
    out.elements.clear();
    return true;
}

bool RESPParser::parseBulkString(RESPObject& out) {
    if (pos_ >= buffer_.size() || buffer_[pos_] != '$') {
        return false;
    }
    ++pos_;

    std::string line;
    if (!readLine(line)) {
        return false;
    }

    const long long len = std::stoll(line);
    if (len < 0) {
        out.type = RESPType::NULL_BULK;
        out.str.clear();
        out.integer = 0;
        out.elements.clear();
        return true;
    }

    if (buffer_.size() - pos_ < static_cast<size_t>(len) + 2) {
        return false;
    }

    out.type = RESPType::BULK_STRING;
    out.str.assign(buffer_.data() + pos_, static_cast<size_t>(len));
    out.integer = 0;
    out.elements.clear();
    pos_ += static_cast<size_t>(len);

    if (pos_ + 1 >= buffer_.size() || buffer_[pos_] != '\r' || buffer_[pos_ + 1] != '\n') {
        throw std::runtime_error("invalid bulk string ending");
    }
    pos_ += 2;
    return true;
}

bool RESPParser::parseArray(RESPObject& out) {
    if (pos_ >= buffer_.size() || buffer_[pos_] != '*') {
        return false;
    }
    ++pos_;

    std::string line;
    if (!readLine(line)) {
        return false;
    }

    const long long count = std::stoll(line);
    if (count < 0) {
        throw std::runtime_error("null array is not supported yet");
    }

    out.type = RESPType::ARRAY;
    out.str.clear();
    out.integer = 0;
    out.elements.clear();
    out.elements.reserve(static_cast<size_t>(count));

    for (long long i = 0; i < count; ++i) {
        RESPObject element;
        if (!parseInternal(element)) {
            return false;
        }
        out.elements.push_back(std::move(element));
    }
    return true;
}

bool RESPParser::readLine(std::string& line) {
    const size_t end = buffer_.find("\r\n", pos_);
    if (end == std::string::npos) {
        return false;
    }
    line.assign(buffer_.data() + pos_, end - pos_);
    pos_ = end + 2;
    return true;
}
