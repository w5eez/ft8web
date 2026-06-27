// SPDX-License-Identifier: GPL-3.0-or-later

#include "TxEngine.hpp"

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <ctime>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <vector>
#include <unistd.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace ft8web {

namespace {
std::string shellQuote(const std::string& s) {
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    out += "'";
    return out;
}

double nowEpoch() {
    return std::chrono::duration<double>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}
}

const TxEngine::ModeSpec* TxEngine::spec(const std::string& mode) {
    static const ModeSpec kFt8 {"ft8sim",  15.0,  13.8,  false, 100, false, 0.0};
    static const ModeSpec kFt4 {"ft4sim",  7.5,   6.2,   false, 100, false, 0.0};
    static const ModeSpec kFt2 {"ft2sim",  3.75,  3.5,   false, 100, false, 0.0};
    static const ModeSpec kWspr{"wsprsim", 120.0, 113.0, true,  25,  true,  0.0};
    if (mode == "FT8")  return &kFt8;
    if (mode == "FT4")  return &kFt4;
    if (mode == "FT2")  return &kFt2;
    if (mode == "WSPR") return &kWspr;
    return nullptr;
}

TxEngine::TxEngine(AudioEngine& audio, std::string simDir, std::string workDir, std::string playDev)
    : audio_(audio), simDir_(std::move(simDir)), workDir_(std::move(workDir)),
      playDev_(std::move(playDev)) {
    std::error_code ec;
    std::filesystem::create_directories(workDir_, ec);
    std::srand(static_cast<unsigned>(std::time(nullptr)));
    thread_ = std::thread(&TxEngine::loop, this);
}

TxEngine::~TxEngine() {
    run_.store(false);
    disarm();
    if (thread_.joinable()) thread_.join();
}

void TxEngine::setHooks(TxHooks h) { hooks_ = std::move(h); }

void TxEngine::setPlayDevice(const std::string& dev) {
    std::lock_guard<std::mutex> lk(mtx_);
    playDev_ = dev;
}

bool TxEngine::arm(const std::string& message, const std::string& mode, std::string& err) {
    const ModeSpec* sp = spec(mode);
    if (!sp) { err = "TX not supported in " + mode + " (FT8/FT4/FT2/WSPR only)"; return false; }
    if (!std::filesystem::exists(simDir_ + "/" + sp->sim)) {
        err = std::string(sp->sim) + " encoder not built";
        return false;
    }
    if (message.empty()) { err = "no message selected"; return false; }
    if (hooks_.inhibited && hooks_.inhibited()) { err = "TX inhibited (SWR protection)"; return false; }
    {
        std::lock_guard<std::mutex> l(mtx_);
        message_ = message;
        modeSpec_ = sp;
        modeName_ = mode;
    }
    lastSlot_.store(-1);
    armed_.store(true);
    return true;
}

void TxEngine::updateMessage(const std::string& message) {
    std::lock_guard<std::mutex> l(mtx_);
    if (!message.empty()) message_ = message;
}

std::string TxEngine::message() const {
    std::lock_guard<std::mutex> l(mtx_);
    return message_;
}

void TxEngine::disarm() {
    armed_.store(false);
    audio_.stopPlayback();
}

void TxEngine::loop() {
    while (run_.load()) {
        usleep(50000);
        if (!armed_.load()) continue;
        if (hooks_.inhibited && hooks_.inhibited()) { disarm(); continue; }

        const ModeSpec* sp;
        {
            std::lock_guard<std::mutex> l(mtx_);
            sp = modeSpec_;
        }
        if (!sp) continue;
        const double period = sp->period;
        const double t = nowEpoch();
        const long long slot = static_cast<long long>(std::floor(t / period));
        const bool slotIsEven = slot % 2 == 0;
        const bool mine = sp->everySlot ? true
                        : hooks_.slotEven ? (hooks_.slotEven() == slotIsEven) : slotIsEven;
        if (!mine || slot == lastSlot_.load()) continue;

        const double into       = t - static_cast<double>(slot) * period;
        const double startDelay = sp->startDelay;
        if (into < startDelay || into > startDelay + 0.25) continue;

        lastSlot_.store(slot);

        if (hooks_.skipSlot && hooks_.skipSlot(slot)) {
            std::fprintf(stderr, "[tx] skipping CQ slot %lld (1-on-1-off)\n", slot);
            continue;
        }
        std::fprintf(stderr, "[tx] fire slot=%lld %s into=%.2fs\n",
                     slot, slotIsEven ? "EVEN" : "ODD", into);
        if (sp->dutyPct < 100 && static_cast<int>(std::rand() % 100) >= sp->dutyPct) {
            std::fprintf(stderr, "[tx] WSPR duty cycle: skipping this slot\n");
            continue;
        }
        transmitOnce();
    }
}

void TxEngine::transmitOnce() {
    std::string msg, modeName;
    const ModeSpec* sp;
    {
        std::lock_guard<std::mutex> l(mtx_);
        msg = message_;
        sp = modeSpec_;
        modeName = modeName_;
    }
    if (!sp || msg.empty()) return;

    if (hooks_.beforeTx) hooks_.beforeTx(msg);

    const std::string wav = workDir_ + "/000000_000001.wav";
    std::error_code ec;
    std::filesystem::remove(wav, ec);

    const int offset = hooks_.offsetHz ? hooks_.offsetHz() : 1500;
    if (sp->wsprFsk) {
        if (!buildWsprWav(msg, offset, wav)) {
            std::fprintf(stderr, "[tx] WSPR encode failed for: %s\n", msg.c_str());
            return;
        }
    } else {
        const std::string cmd = "cd " + shellQuote(workDir_) + " && " +

            shellQuote(simDir_ + "/" + sp->sim) + " " + shellQuote(msg) + " " +
            std::to_string(offset) + " 0.0 0.0 0.0 1 99 >/dev/null 2>&1";
        std::system(cmd.c_str());
    }
    if (!std::filesystem::exists(wav)) {
        std::fprintf(stderr, "[tx] encoder produced no wav for: %s\n", msg.c_str());
        return;
    }

    const float level = hooks_.level01 ? hooks_.level01() : 0.5f;

    if (!armed_.load() || (hooks_.inhibited && hooks_.inhibited())) return;
    std::fprintf(stderr, "[tx] sending \"%s\" @ %d Hz\n", msg.c_str(), offset);
    if (hooks_.ptt) hooks_.ptt(true);
    active_.store(true);

    if (hooks_.sent && !sp->wsprFsk) hooks_.sent(msg, modeName, offset);
    std::string dev;
    { std::lock_guard<std::mutex> lk(mtx_); dev = playDev_; }
    const bool played = audio_.playWavFile(dev, wav, level, sp->playSeconds);
    active_.store(false);
    if (hooks_.ptt) hooks_.ptt(false);
    if (!played)
        std::fprintf(stderr, "[tx] WARNING: playback failed (device busy?) — keyed with no modulation\n");
    if (hooks_.txDone) hooks_.txDone();
}

bool TxEngine::buildWsprWav(const std::string& msg, int offsetHz, const std::string& wavPath) {
    const std::string cmd = shellQuote(simDir_ + "/wsprsim") + " -c " + shellQuote(msg) + " 2>/dev/null";
    FILE* pp = ::popen(cmd.c_str(), "r");
    if (!pp) return false;
    std::vector<int> syms;
    char line[512];
    bool grab = false;
    while (std::fgets(line, sizeof line, pp)) {
        std::string s(line);
        if (s.find("Channel symbols") != std::string::npos) { grab = true; continue; }
        if (!grab) continue;
        for (char c : s)
            if (c >= '0' && c <= '3') syms.push_back(c - '0');
    }
    ::pclose(pp);
    if (syms.size() < 162) return false;
    syms.resize(162);

    const int    rate    = 12000;
    const int    sps     = 8192;
    const double spacing = 12000.0 / 8192.0;
    std::vector<int16_t> pcm;
    pcm.reserve(static_cast<size_t>(rate) * 113);
    pcm.insert(pcm.end(), rate, 0);
    double phase = 0.0;
    for (int sym : syms) {
        const double f  = offsetHz + (sym - 1.5) * spacing;
        const double dp = 2.0 * M_PI * f / rate;
        for (int i = 0; i < sps; ++i) {
            phase += dp;
            pcm.push_back(static_cast<int16_t>(0.9 * 32767.0 * std::sin(phase)));
        }
    }
    pcm.insert(pcm.end(), rate / 2, 0);

    std::ofstream f(wavPath, std::ios::binary);
    if (!f) return false;
    const uint32_t dataBytes = static_cast<uint32_t>(pcm.size() * 2);
    auto w32 = [&](uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    auto w16 = [&](uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
    f.write("RIFF", 4); w32(36 + dataBytes); f.write("WAVE", 4);
    f.write("fmt ", 4); w32(16); w16(1); w16(1); w32(rate); w32(rate * 2); w16(2); w16(16);
    f.write("data", 4); w32(dataBytes);
    f.write(reinterpret_cast<const char*>(pcm.data()), dataBytes);
    return static_cast<bool>(f);
}

}
