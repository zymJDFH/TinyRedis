#include "../../include/config/serverConfig.hpp"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstring>
#include <fstream>
#include <sstream>
#include <utility>
#include <vector>

namespace {
std::string trim(const std::string& s) {
    size_t begin = 0;
    while (begin < s.size() && std::isspace(static_cast<unsigned char>(s[begin]))) {
        ++begin;
    }

    size_t end = s.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
        --end;
    }

    return s.substr(begin, end - begin);
}

std::string toLowerCopy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

std::vector<std::string> splitWords(const std::string& line) {
    std::istringstream iss(line);
    std::vector<std::string> out;
    std::string token;
    while (iss >> token) {
        out.push_back(token);
    }
    return out;
}

bool parseYesNo(const std::string& value, bool& out) {
    const std::string lowered = toLowerCopy(value);
    if (lowered == "yes" || lowered == "true" || lowered == "1") {
        out = true;
        return true;
    }
    if (lowered == "no" || lowered == "false" || lowered == "0") {
        out = false;
        return true;
    }
    return false;
}

bool parsePort(const std::string& value, int& out) {
    try {
        size_t parsed = 0;
        const long port = std::stol(value, &parsed, 10);
        if (parsed != value.size() || port <= 0 || port > 65535) {
            return false;
        }
        out = static_cast<int>(port);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}
} // namespace

bool parseAofFsyncPolicy(const std::string& value, AofFsyncPolicy& policy) {
    const std::string lowered = toLowerCopy(value);
    if (lowered == "always") {
        policy = AofFsyncPolicy::Always;
        return true;
    }
    if (lowered == "everysec") {
        policy = AofFsyncPolicy::EverySec;
        return true;
    }
    if (lowered == "no") {
        policy = AofFsyncPolicy::No;
        return true;
    }
    return false;
}

bool loadServerConfig(const std::string& path, ServerConfig& config, std::string& err) {
    err.clear();

    errno = 0;
    std::ifstream in(path);
    if (!in.is_open()) {
        const std::string reason = errno == 0 ? "open failed" : std::strerror(errno);
        err = "cannot open config file '" + path + "': " + reason;
        return false;
    }

    ServerConfig parsed = config;
    std::string line;
    int lineNo = 0;

    while (std::getline(in, line)) {
        ++lineNo;

        const size_t comment = line.find('#');
        if (comment != std::string::npos) {
            line.erase(comment);
        }

        line = trim(line);
        if (line.empty()) {
            continue;
        }

        const std::vector<std::string> tokens = splitWords(line);
        if (tokens.empty()) {
            continue;
        }

        const std::string key = toLowerCopy(tokens[0]);

        if (key == "replicaof" || key == "slaveof") {
            if (tokens.size() != 3) {
                err = path + ":" + std::to_string(lineNo) + ": replicaof expects '<host> <port>'";
                return false;
            }

            const std::string host = tokens[1];
            const std::string portValue = tokens[2];
            if (toLowerCopy(host) == "no" && toLowerCopy(portValue) == "one") {
                parsed.replication.becomeMaster();
                continue;
            }

            int masterPort = 0;
            if (!parsePort(portValue, masterPort)) {
                err = path + ":" + std::to_string(lineNo) + ": invalid replicaof port '" + portValue + "'";
                return false;
            }
            parsed.replication.becomeReplica(host, masterPort);
            continue;
        }

        if (tokens.size() != 2) {
            err = path + ":" + std::to_string(lineNo) + ": expected '<directive> <value>'";
            return false;
        }

        const std::string& value = tokens[1];

        if (key == "port") {
            if (!parsePort(value, parsed.port)) {
                err = path + ":" + std::to_string(lineNo) + ": invalid port '" + value + "'";
                return false;
            }
            continue;
        }

        if (key == "appendonly") {
            if (!parseYesNo(value, parsed.appendOnly)) {
                err = path + ":" + std::to_string(lineNo) + ": appendonly expects yes/no";
                return false;
            }
            continue;
        }

        if (key == "appendfilename") {
            parsed.appendFilename = value;
            continue;
        }

        if (key == "appendfsync") {
            if (!parseAofFsyncPolicy(value, parsed.appendFsync)) {
                err = path + ":" + std::to_string(lineNo) + ": appendfsync expects always/everysec/no";
                return false;
            }
            continue;
        }

        err = path + ":" + std::to_string(lineNo) + ": unknown directive '" + tokens[0] + "'";
        return false;
    }

    config = std::move(parsed);
    return true;
}
