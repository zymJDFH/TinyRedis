#pragma once

#include "inMemoryDB.hpp"
#include "../persistentence/aof.hpp"
#include <string>
#include <vector>

class CommandDispatcher {
public:
    CommandDispatcher(bool enableAof = false, std::string aofPath = "appendonly.aof");

    std::string dispatch(const std::vector<std::string>& argv);
    bool loadAof();
    bool rewriteAof(std::string& err);
    const std::string& lastError() const;
    // 事件循环周期性调用：执行一次主动过期扫描。
    void cron();

private:
    std::string dispatchInternal(const std::vector<std::string>& argv, bool replayingAof);

private:
    InMemoryDB db_;
    AOF aof_;
    std::string lastError_;
};
