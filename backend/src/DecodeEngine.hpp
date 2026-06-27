// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "AudioEngine.hpp"

#include <cstdio>
#include <string>
#include <vector>
#include <deque>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>

namespace ft8web {

struct Decode {
    std::string utc;
    int         snr  = 0;
    double      dt   = 0.0;
    int         freq = 0;
    std::string mode;
    std::string band;
    std::string message;
    bool        lowConf = false;   // jt9 flagged the decode as low-confidence (qual < 3)
};

struct ModeSpec {
    std::string name;
    std::string flag;
    double      period;
};

class DecodeEngine {
public:
    DecodeEngine(AudioEngine& audio, std::string jt9Path, std::string wsprdPath,
                 std::string workDir);

    void setDialGetter(std::function<long long()> f) { dialHz_ = std::move(f); }
    ~DecodeEngine();
    DecodeEngine(const DecodeEngine&) = delete;
    DecodeEngine& operator=(const DecodeEngine&) = delete;

    static const std::vector<ModeSpec>& modes();
    static const ModeSpec*               findMode(const std::string& name);

    void        setMode(const std::string& name);
    std::string mode() const;
    double      periodSeconds() const;

    void setRxFreq(int hz) { rxFreq_.store(hz); }
    int  rxFreq() const    { return rxFreq_.load(); }

    void setStation(const std::string& call, const std::string& grid) {
        std::lock_guard<std::mutex> lk(stationMtx_);
        myCall_ = call; myGrid_ = grid;
    }

    void setApContext(std::function<void(std::string&, std::string&, int&)> f) {
        std::lock_guard<std::mutex> lk(stationMtx_);
        apCtx_ = std::move(f);
    }

    void setJt9dPath(const std::string& path) { jt9dPath_ = path; }

    void setCallback(std::function<void(const Decode&)> cb);

    void start();
    void stop();
    bool running() const { return running_.load(); }

    std::vector<Decode> recent(size_t n = 100) const;

    struct DecodeStat { std::string mode; double wall_s = 0; int count = 0; };
    std::vector<DecodeStat> recentStats() const;

private:
    void loop();
    std::vector<Decode> runDecoder(const std::vector<int16_t>& samples,
                                   const ModeSpec& spec, const std::string& stamp,
                                   long long dial);
    bool startJt9d();
    void stopJt9d();
    bool decodeViaJt9d(const std::string& wav, int nutc, const std::string& band,
                       const ModeSpec& spec, std::vector<Decode>& out);
    static bool writeWav(const std::string& path, const std::vector<int16_t>& samples, int rate);
    static bool parseLine(const std::string& line, const std::string& mode, Decode& out);
    static bool parseWsprLine(const std::string& line, long long dialHz, Decode& out);

    AudioEngine& audio_;
    std::string  jt9_;
    std::string  wsprd_;
    std::string  workDir_;
    std::function<long long()> dialHz_;

    mutable std::mutex modeMtx_;
    ModeSpec           mode_;
    std::atomic<int>   rxFreq_{1500};

    mutable std::mutex stationMtx_;
    std::string        myCall_, myGrid_;
    std::function<void(std::string&, std::string&, int&)> apCtx_;

    std::string jt9dPath_;
    int         jt9dPid_ = -1;
    int         jt9dIn_  = -1;
    std::FILE*  jt9dOut_ = nullptr;

    std::mutex                          cbMtx_;
    std::function<void(const Decode&)>  cb_;

    std::atomic<bool> running_{false};
    std::thread       thread_;

    mutable std::mutex recentMtx_;
    std::deque<Decode> recent_;

    mutable std::mutex     statMtx_;
    std::deque<DecodeStat> stats_;
};

}
