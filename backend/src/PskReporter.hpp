// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <thread>
#include <atomic>
#include <cstdint>

namespace ft8web {

struct RxReport {
    std::string receiver;
    std::string locator;
    long long   freq = 0;
    std::string mode;
    int         snr = 0;
    long long   t = 0;
};

class PskReporter {
public:
    PskReporter();
    ~PskReporter();
    PskReporter(const PskReporter&) = delete;
    PskReporter& operator=(const PskReporter&) = delete;

    void configure(const std::string& callsign, const std::string& grid, bool enabled);

    void addDecode(const std::string& message, int snr, int audioOffsetHz,
                   const std::string& mode, long long dialHz);

    std::vector<RxReport> reports() const;
    long long lastQuerySec() const { return lastQueryDone_.load(); }
    long long lastUploadSec() const { return lastUploadDone_.load(); }

    static std::string parseSender(const std::string& msg);
    static std::string parseGrid(const std::string& msg);

private:
    struct Spot {
        std::string call, locator, mode;
        long long   freq = 0;
        int         snr = 0;
        long long   t = 0;
    };

    void loop();
    void uploadBatch();
    void runQuery();
    bool gated() const;
    std::vector<uint8_t> buildPacket(const std::vector<Spot>& spots);

    mutable std::mutex cfgMtx_;
    std::string call_, grid_;
    std::atomic<bool> enabled_{false};

    std::mutex            bufMtx_;
    std::map<std::string, Spot> buf_;

    mutable std::mutex    repMtx_;
    std::vector<RxReport> reports_;

    std::atomic<long long> lastUploadDone_{0};
    std::atomic<long long> lastQueryDone_{0};
    uint32_t  obsDomain_ = 0;
    uint32_t  seq_ = 0;

    std::atomic<bool> run_{true};
    std::thread       thread_;
};

}
