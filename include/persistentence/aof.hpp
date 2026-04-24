#pragma once

#include <functional>
#include <chrono>
#include <future>
#include <string>
#include <vector>

enum class AofFsyncPolicy {
    Always,
    EverySec,
    No,
};

const char* aofFsyncPolicyName(AofFsyncPolicy policy);

class AOF {
public:
    AOF(bool enabled = false,
        std::string path = "appendonly.aof",
        AofFsyncPolicy fsyncPolicy = AofFsyncPolicy::Always);

    bool enabled() const;
    void setEnabled(bool enabled);
    const std::string& path() const;
    AofFsyncPolicy fsyncPolicy() const;

    // 追加一条命令到 AOF（RESP array 编码）。
    bool appendCommand(const std::vector<std::string>& argv, std::string& err);
    // 用一组恢复命令重写 AOF。先写临时文件，再原子替换目标文件。
    bool rewriteCommands(const std::vector<std::vector<std::string>>& commands, std::string& err);
    // 启动后台 AOF rewrite；真正文件替换在 pollBackgroundRewrite 中收尾。
    bool startBackgroundRewrite(const std::vector<std::vector<std::string>>& commands, std::string& err);
    // 事件循环周期调用：若后台 rewrite 已完成，则合并 rewrite buffer 并原子切换文件。
    bool pollBackgroundRewrite(std::string& err);
    bool backgroundRewriteInProgress() const;
    const std::string& lastBackgroundRewriteStatus() const;
    // appendfsync everysec 使用：事件循环周期调用，必要时刷盘。
    bool flushIfNeeded(std::string& err, bool force = false);

    // 从 AOF 读取并顺序回放。apply 返回 false 时表示回放失败。
    bool replay(const std::function<bool(const std::vector<std::string>&, std::string&)>& apply,
                std::string& err) const;

private:
    bool fsyncPath(std::string& err);
    bool appendPayloadToFile(const std::string& path, const std::string& payload, bool append, std::string& err) const;

    struct BackgroundRewriteResult {
        bool ok = false;
        std::string tmpPath;
        std::string err;
    };

private:
    bool enabled_;
    std::string path_;
    AofFsyncPolicy fsyncPolicy_;
    bool dirty_;
    std::chrono::steady_clock::time_point lastFsync_;
    bool backgroundRewriteInProgress_;
    std::future<BackgroundRewriteResult> backgroundRewriteFuture_;
    std::string backgroundRewriteBuffer_;
    std::string lastBackgroundRewriteStatus_;
};
