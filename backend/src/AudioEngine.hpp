// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>
#include <cstdint>

namespace ft8web {

struct AudioDevice {
    std::string id;
    std::string name;
};

class AudioEngine {
public:
    static constexpr int kSampleRate = 12000;

    AudioEngine() = default;
    ~AudioEngine();
    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    static std::vector<AudioDevice> listCaptureDevices();

    static std::string resolveDevice(const std::string& stored);

    bool start(const std::string& device);
    void stop();
    bool running() const { return running_.load(); }

    std::string device() const { std::lock_guard<std::mutex> lk(mtx_); return device_; }
    std::string error()  const { std::lock_guard<std::mutex> lk(errMtx_); return lastError_; }

    bool silent() const;

    void setTransmitting(bool on);

    float level() const { return level_.load(); }
    bool  clipping() const;
    long long xruns() const { return xruns_.load(); }

    int  captureVolume() const;
    void setCaptureVolume(int pct);
    bool hasCaptureVolume() const { return hasCapVol_; }

    void setChannel(int ch) { chSel_.store(ch > 0 ? 1 : 0); }

    bool playWavFile(const std::string& device, const std::string& path,
                     float amplitude, double maxSeconds);
    void stopPlayback() { playRun_.store(false); }

    std::vector<int16_t> latest(size_t n) const;

    std::vector<int16_t> extractPeriod(std::chrono::system_clock::time_point start,
                                       double seconds) const;

private:
    void captureLoop();
    void openMixer();
    void closeMixer();
    void stopLocked();
    static std::string cardOf(const std::string& dev);

    std::mutex        ctlMtx_;
    std::string       device_;
    std::string       pcm_;
    std::atomic<bool> running_{false};
    std::thread       thread_;

    mutable std::mutex     errMtx_;
    std::string            lastError_;
    std::atomic<long long> lastLoudMs_{0};
    std::atomic<long long> captureStartMs_{0};
    std::atomic<bool>      txActive_{false};

    std::atomic<int>  chSel_{0};
    std::atomic<bool> playRun_{false};
    std::atomic<float>     level_{0.0f};
    std::atomic<long long> lastClipMs_{0};
    std::atomic<long long> xruns_{0};

    mutable std::mutex                     mtx_;
    std::vector<int16_t>                   ring_;
    size_t                                 cap_     = 0;
    uint64_t                               written_ = 0;
    std::chrono::system_clock::time_point  lastTime_;

    void*              mixer_     = nullptr;
    void*              capElem_   = nullptr;
    bool               hasCapVol_ = false;
    mutable std::mutex mixMtx_;
};

}
