// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "LogStore.hpp"

#include <string>
#include <vector>
#include <deque>
#include <mutex>
#include <thread>
#include <atomic>

namespace ft8web {

class Uploader {
public:
    explicit Uploader(std::string queuePath);
    ~Uploader();
    Uploader(const Uploader&) = delete;
    Uploader& operator=(const Uploader&) = delete;

    void configure(const std::string& callsign, const std::string& grid,
                   bool lotwEnabled, const std::string& qrzApiKey, bool qrzEnabled);

    void enqueue(QsoFields q);

    struct Status {
        bool lotwEnabled = false, qrzEnabled = false;
        int  pending = 0, failed = 0, done = 0;
        std::vector<std::string> recent;
    };
    Status status() const;

    void activity(const std::string& line);

private:
    struct Item {
        long long  id = 0;
        long long  t = 0;
        QsoFields  q;
        std::string lotw = "skip";
        std::string qrz  = "skip";
        int        attempts = 0;
        long long  nextTry = 0;
    };

    void loop();
    void process(Item& it);
    bool uploadLotw(const Item& it, std::string& err, bool& permanent);
    bool uploadQrz(const Item& it, std::string& err, bool& permanent);
    void writeDynamicStationLocation(const std::string& gridOverride = "") const;
    void load();
    void save() const;
    void note(const std::string& line);
    static std::string runCmd(const std::string& cmd, int& exitCode);

    std::string queuePath_;
    std::string activityPath_;

    mutable std::mutex cfgMtx_;
    std::string call_, grid_, qrzKey_;
    std::atomic<bool> lotwOn_{false}, qrzOn_{false};

    mutable std::mutex itemsMtx_;
    std::vector<Item>  items_;
    std::deque<std::string> recent_;
    long long nextId_ = 1;

    std::atomic<bool> run_{true};
    std::thread       thread_;
};

}
