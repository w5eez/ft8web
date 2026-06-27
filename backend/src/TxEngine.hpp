// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "AudioEngine.hpp"

#include <string>
#include <functional>
#include <mutex>
#include <thread>
#include <atomic>

namespace ft8web {

struct TxHooks {
    std::function<bool()>     inhibited;
    std::function<int()>      offsetHz;
    std::function<bool()>     slotEven;
    std::function<bool(long long)> skipSlot;
    std::function<float()>    level01;
    std::function<void(bool)> ptt;

    std::function<void(const std::string&)> beforeTx;
    std::function<void(const std::string&, const std::string&, int)> sent;
    std::function<void()>     txDone;
};

class TxEngine {
public:
    TxEngine(AudioEngine& audio, std::string simDir, std::string workDir, std::string playDev);
    ~TxEngine();
    TxEngine(const TxEngine&) = delete;
    TxEngine& operator=(const TxEngine&) = delete;

    void setHooks(TxHooks h);
    void setPlayDevice(const std::string& dev);

    bool arm(const std::string& message, const std::string& mode, std::string& err);
    void updateMessage(const std::string& message);
    void disarm();
    std::string message() const;

    bool armed() const  { return armed_.load(); }
    bool active() const { return active_.load(); }

private:
    struct ModeSpec {
        const char* sim;
        double period;
        double playSeconds;
        bool   everySlot;
        int    dutyPct;
        bool   wsprFsk;
        double startDelay;
    };
    static const ModeSpec* spec(const std::string& mode);

    void loop();
    void transmitOnce();
    bool buildWsprWav(const std::string& msg, int offsetHz, const std::string& wavPath);

    AudioEngine& audio_;
    std::string  simDir_, workDir_, playDev_;

    TxHooks            hooks_;
    mutable std::mutex mtx_;
    std::string        message_;
    std::string        modeName_;
    const ModeSpec*    modeSpec_ = nullptr;

    std::atomic<bool>  armed_{false};
    std::atomic<bool>  active_{false};
    std::atomic<long long> lastSlot_{-1};

    std::atomic<bool>  run_{true};
    std::thread        thread_;
};

}
