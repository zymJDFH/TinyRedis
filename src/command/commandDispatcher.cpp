#include "../../include/command/commandDispatcher.hpp"

#include "../../include/protocol/respEncoder.hpp"
#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace {
constexpr size_t kActiveExpireSampleCount = 64;

std::string toUpperCopy(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return out;
}

std::string wrongArity(const std::string& cmd) {
    return RESPEncoder::error("ERR wrong number of arguments for '" + cmd + "' command");
}

bool parseInt64(const std::string& s, long long& out) {
    // Redis 风格数字参数解析：必须完整匹配整个字符串。
    try {
        size_t parsed = 0;
        const long long value = std::stoll(s, &parsed, 10);
        if (parsed != s.size()) {
            return false;
        }
        out = value;
        return true;
    } catch (const std::exception&) {
        return false;
    }
}
} // namespace

void CommandDispatcher::cron() {
    (void)db_.activeExpireCycle(kActiveExpireSampleCount);
}

std::string CommandDispatcher::dispatch(const std::vector<std::string>& argv) {
    if (argv.empty()) {
        return RESPEncoder::error("ERR empty command");
    }

    const std::string cmd = toUpperCopy(argv[0]);

    if (cmd == "PING") {
        if (argv.size() == 1) {
            return RESPEncoder::simpleString("PONG");
        }
        if (argv.size() == 2) {
            return RESPEncoder::bulkString(argv[1]);
        }
        return wrongArity("ping");
    }

    if (cmd == "SET") {
        if (argv.size() != 3) {
            return wrongArity("set");
        }
        db_.set(argv[1], argv[2]);
        return RESPEncoder::simpleString("OK");
    }

    if (cmd == "GET") {
        if (argv.size() != 2) {
            return wrongArity("get");
        }
        std::string value;
        if (!db_.get(argv[1], value)) {
            return RESPEncoder::nullBulk();
        }
        return RESPEncoder::bulkString(value);
    }

    if (cmd == "DEL") {
        if (argv.size() < 2) {
            return wrongArity("del");
        }
        long long removed = 0;
        for (size_t i = 1; i < argv.size(); ++i) {
            removed += db_.del(argv[i]);
        }
        return RESPEncoder::integer(removed);
    }

    if (cmd == "EXISTS") {
        if (argv.size() != 2) {
            return wrongArity("exists");
        }
        return RESPEncoder::integer(db_.exists(argv[1]) ? 1 : 0);
    }

    if (cmd == "INCR") {
        if (argv.size() != 2) {
            return wrongArity("incr");
        }

        long long newValue = 0;
        std::string err;
        if (!db_.incr(argv[1], newValue, err)) {
            return RESPEncoder::error("ERR " + err);
        }
        return RESPEncoder::integer(newValue);
    }

    if (cmd == "EXPIRE") {
        if (argv.size() != 3) {
            return wrongArity("expire");
        }

        long long ttlSeconds = 0;
        if (!parseInt64(argv[2], ttlSeconds)) {
            return RESPEncoder::error("ERR value is not an integer or out of range");
        }

        return RESPEncoder::integer(db_.expire(argv[1], ttlSeconds));
    }

    if (cmd == "TTL") {
        if (argv.size() != 2) {
            return wrongArity("ttl");
        }
        return RESPEncoder::integer(db_.ttl(argv[1]));
    }

    if (cmd == "PTTL") {
        if (argv.size() != 2) {
            return wrongArity("pttl");
        }
        return RESPEncoder::integer(db_.pttl(argv[1]));
    }

    if (cmd == "PERSIST") {
        if (argv.size() != 2) {
            return wrongArity("persist");
        }
        return RESPEncoder::integer(db_.persist(argv[1]));
    }

    return RESPEncoder::error("ERR unknown command '" + argv[0] + "'");
}
