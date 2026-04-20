#pragma once

#include <functional>
#include <string>
#include <vector>

class AOF {
public:
    AOF(bool enabled = false, std::string path = "appendonly.aof");

    bool enabled() const;
    void setEnabled(bool enabled);
    const std::string& path() const;

    // 追加一条命令到 AOF（RESP array 编码）。
    bool appendCommand(const std::vector<std::string>& argv, std::string& err) const;
    // 用一组恢复命令重写 AOF。先写临时文件，再原子替换目标文件。
    bool rewriteCommands(const std::vector<std::vector<std::string>>& commands, std::string& err) const;

    // 从 AOF 读取并顺序回放。apply 返回 false 时表示回放失败。
    bool replay(const std::function<bool(const std::vector<std::string>&, std::string&)>& apply,
                std::string& err) const;

private:
    bool enabled_;
    std::string path_;
};
