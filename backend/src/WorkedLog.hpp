// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>
#include <set>
#include <vector>
#include <mutex>
#include <atomic>
#include <thread>
#include <condition_variable>

namespace ft8web {

class WorkedLog {
public:
    explicit WorkedLog(std::string cacheFile);
    ~WorkedLog();
    WorkedLog(const WorkedLog&) = delete;
    WorkedLog& operator=(const WorkedLog&) = delete;

    void configure(const std::string& qrzKey);
    void requestSync();

    struct Snapshot {
        std::vector<std::string> calls;
        std::vector<std::string> slots;
        std::vector<std::string> grids;
        int       qsos = 0;
        long long syncedSec = 0;
        bool      syncing = false;
        bool      haveKey = false;
        std::string error;
    };
    Snapshot snapshot() const;

private:
    void loop();
    bool fetchAndRebuild();
    void rebuild(const std::string& adif);
    void loadCache();

    std::string cacheFile_;
    mutable std::mutex mtx_;
    std::set<std::string> calls_, slots_, grids_;
    int         qsos_ = 0;
    std::string qrzKey_;
    long long   syncedSec_ = 0;
    std::string error_;
    std::atomic<bool> syncing_{false};

    std::mutex              cvMtx_;
    std::condition_variable cv_;
    std::atomic<bool>       run_{true};
    std::atomic<bool>       syncReq_{false};
    std::thread             thread_;
};

}
