#pragma once

#include "inMemoryDB.hpp"
#include <string>
#include <vector>

class CommandDispatcher {
public:
    std::string dispatch(const std::vector<std::string>& argv);
    // 事件循环周期性调用：执行一次主动过期扫描。
    void cron();

private:
    InMemoryDB db_;
};
