// SPDX-License-Identifier: GPL-3.0-or-later

#include "DecodeEngine.hpp"
#include "FreqPlan.hpp"

#include <fstream>
#include <sstream>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <cmath>
#include <ctime>
#include <chrono>
#include <cstdlib>
#include <thread>
#include <filesystem>

#include <unistd.h>
#include <sys/wait.h>
#include <csignal>

namespace ft8web {

DecodeEngine::DecodeEngine(AudioEngine& audio, std::string jt9Path, std::string wsprdPath,
                           std::string workDir)
    : audio_(audio), jt9_(std::move(jt9Path)), wsprd_(std::move(wsprdPath)),
      workDir_(std::move(workDir)) {
    std::error_code ec;
    std::filesystem::create_directories(workDir_, ec);
    mode_ = modes().front();
}

DecodeEngine::~DecodeEngine() { stop(); stopJt9d(); }

void DecodeEngine::setMode(const std::string& name) {
    if (const ModeSpec* m = findMode(name)) {
        std::lock_guard<std::mutex> lk(modeMtx_);
        mode_ = *m;
    }
}

std::string DecodeEngine::mode() const {
    std::lock_guard<std::mutex> lk(modeMtx_);
    return mode_.name;
}

double DecodeEngine::periodSeconds() const {
    std::lock_guard<std::mutex> lk(modeMtx_);
    return mode_.period;
}

void DecodeEngine::setCallback(std::function<void(const Decode&)> cb) {
    std::lock_guard<std::mutex> lk(cbMtx_);
    cb_ = std::move(cb);
}

void DecodeEngine::start() {
    if (running_.load()) return;
    running_.store(true);
    thread_ = std::thread(&DecodeEngine::loop, this);
}

void DecodeEngine::stop() {
    running_.store(false);
    if (thread_.joinable()) thread_.join();
}

std::vector<Decode> DecodeEngine::recent(size_t n) const {
    std::lock_guard<std::mutex> lk(recentMtx_);
    if (n >= recent_.size()) return {recent_.begin(), recent_.end()};
    return {recent_.end() - static_cast<long>(n), recent_.end()};
}

std::vector<DecodeEngine::DecodeStat> DecodeEngine::recentStats() const {
    std::lock_guard<std::mutex> lk(statMtx_);
    return {stats_.begin(), stats_.end()};
}

void DecodeEngine::loop() {
    using namespace std::chrono;
    double lastProcessed = -1.0;

    while (running_.load()) {
        ModeSpec spec;
        { std::lock_guard<std::mutex> lk(modeMtx_); spec = mode_; }
        const double P = spec.period;

        double decodeAt = P + 0.25;
        double captureS = P;
        if (spec.name == "FT8")       { decodeAt = 13.4;   captureS = 13.3; }
        else if (spec.name == "FT4")  { decodeAt = 5.9;    captureS = 5.8; }
        else if (spec.name == "WSPR") { decodeAt = P + 0.8; captureS = 114.0; }

        double nowSec   = duration<double>(system_clock::now().time_since_epoch()).count();
        double curStart = std::floor(nowSec / P) * P;
        double wake     = curStart + decodeAt;

        const long long periodDial = (curStart != lastProcessed && dialHz_) ? dialHz_() : 0;

        while (running_.load()) {
            double t = duration<double>(system_clock::now().time_since_epoch()).count();
            if (t >= wake) break;
            { std::lock_guard<std::mutex> lk(modeMtx_);
              if (mode_.name != spec.name || mode_.period != P) break; }
            std::this_thread::sleep_for(milliseconds(100));
        }
        if (!running_.load()) break;
        { std::lock_guard<std::mutex> lk(modeMtx_); if (mode_.name != spec.name) continue; }
        if (curStart == lastProcessed) { std::this_thread::sleep_for(milliseconds(200)); continue; }
        lastProcessed = curStart;

        const bool wspr = spec.name == "WSPR";

        if (wspr && periodDial <= 0) continue;
        auto periodStart = system_clock::time_point{} +
                           duration_cast<system_clock::duration>(duration<double>(curStart));
        auto samples = audio_.extractPeriod(periodStart, captureS);
        if (samples.size() < static_cast<size_t>(captureS * AudioEngine::kSampleRate * 0.9))
            continue;

        std::time_t tt = static_cast<std::time_t>(curStart);
        std::tm tmv{};
        gmtime_r(&tt, &tmv);
        char stamp[20];

        std::strftime(stamp, sizeof stamp, wspr ? "%y%m%d_%H%M" : "%y%m%d_%H%M%S", &tmv);

        const auto decT0 = std::chrono::steady_clock::now();
        auto decodes = runDecoder(samples, spec, stamp, periodDial);
        const double decSec = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - decT0).count();

        if (!wspr)
            std::fprintf(stderr, "[decode] %s: %.2fs wall, %zu decode(s) (slot in %.1fs)\n",
                         spec.name.c_str(), decSec, decodes.size(), P - decodeAt);
        {
            std::lock_guard<std::mutex> lk(statMtx_);
            stats_.push_back({spec.name, decSec, static_cast<int>(decodes.size())});
            while (stats_.size() > 40) stats_.pop_front();
        }
        for (auto& d : decodes) {
            { std::lock_guard<std::mutex> lk(recentMtx_);
              recent_.push_back(d);
              while (recent_.size() > 3000) recent_.pop_front(); }
            std::function<void(const Decode&)> cb;
            { std::lock_guard<std::mutex> lk(cbMtx_); cb = cb_; }
            if (cb) cb(d);
        }
    }
}

std::vector<Decode> DecodeEngine::runDecoder(const std::vector<int16_t>& samples,
                                             const ModeSpec& spec, const std::string& stamp,
                                             long long dial) {
    std::vector<Decode> out;
    const std::string wav = workDir_ + "/" + stamp + ".wav";
    if (!writeWav(wav, samples, AudioEngine::kSampleRate)) return out;

    const std::string band = dial > 0 ? FreqPlan::bandFor(dial) : "";

    if (spec.name != "WSPR" && startJt9d()) {
        const int nutc = stamp.size() >= 6
            ? std::atoi(stamp.substr(stamp.size() - 6).c_str()) : 0;
        if (decodeViaJt9d(wav, nutc, band, spec, out)) {
            std::error_code ec;
            std::filesystem::remove(wav, ec);
            return out;
        }
        out.clear();
    }

    std::ostringstream cmd;
    if (spec.name == "WSPR") {
        char mhz[24];
        std::snprintf(mhz, sizeof mhz, "%.6f", dial / 1e6);
        cmd << wsprd_ << " -d -a " << workDir_ << " -f " << mhz << ' ' << wav << " 2>&1";
    } else {
        char pbuf[16];
        std::snprintf(pbuf, sizeof pbuf, "%.4g", spec.period);

        const int depth = 2;
        std::string myCall, myGrid;
        { std::lock_guard<std::mutex> lk(stationMtx_); myCall = myCall_; myGrid = myGrid_; }
        cmd << jt9_ << ' ' << spec.flag << " -d " << depth << " -f " << rxFreq_.load() << " -p " << pbuf;
        if (!myCall.empty()) {
            cmd << " -c " << myCall;
            if (!myGrid.empty()) cmd << " -G " << myGrid.substr(0, 4);
        }
        cmd << " -a " << workDir_ << " -t " << workDir_ << ' ' << wav << " 2>&1";
    }

    FILE* pp = ::popen(cmd.str().c_str(), "r");
    if (!pp) return out;
    char line[1024];
    while (std::fgets(line, sizeof line, pp)) {
        Decode d;
        const bool ok = spec.name == "WSPR" ? parseWsprLine(line, dial, d)
                                            : parseLine(line, spec.name, d);
        if (ok) {
            d.band = band;
            out.push_back(std::move(d));
        }
    }
    ::pclose(pp);

    std::error_code ec;
    std::filesystem::remove(wav, ec);
    return out;
}

bool DecodeEngine::parseWsprLine(const std::string& line, long long dialHz, Decode& d) {
    std::istringstream ss(line);
    std::string utc;
    int snr = 0, drift = 0;
    double dt = 0.0, mhz = 0.0;
    if (!(ss >> utc >> snr >> dt >> mhz >> drift)) return false;
    if (utc.size() != 4) return false;
    for (char c : utc) if (!std::isdigit(static_cast<unsigned char>(c))) return false;

    std::string msg, tok;
    while (ss >> tok) {
        if (!msg.empty()) msg += ' ';
        msg += tok;
    }
    if (msg.empty()) return false;

    d.utc     = utc + "00";
    d.snr     = snr;
    d.dt      = dt;
    d.freq    = static_cast<int>(std::llround(mhz * 1e6 - static_cast<double>(dialHz)));
    d.mode    = "WSPR";
    d.message = msg;
    return true;
}

bool DecodeEngine::writeWav(const std::string& path, const std::vector<int16_t>& s, int rate) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    const uint32_t dataBytes  = static_cast<uint32_t>(s.size() * 2);
    const uint32_t chunkSize  = 36 + dataBytes;
    const uint16_t fmt = 1, ch = 1, bits = 16;
    const uint32_t sr  = static_cast<uint32_t>(rate);
    const uint32_t byteRate   = sr * ch * bits / 8;
    const uint16_t blockAlign = ch * bits / 8;
    auto w32 = [&](uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    auto w16 = [&](uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
    f.write("RIFF", 4); w32(chunkSize); f.write("WAVE", 4);
    f.write("fmt ", 4); w32(16); w16(fmt); w16(ch); w32(sr); w32(byteRate); w16(blockAlign); w16(bits);
    f.write("data", 4); w32(dataBytes);
    f.write(reinterpret_cast<const char*>(s.data()), dataBytes);
    return static_cast<bool>(f);
}

bool DecodeEngine::parseLine(const std::string& line, const std::string& mode, Decode& d) {

    std::istringstream ss(line);
    std::string utc, sync;
    int snr = 0, freq = 0;
    double dt = 0.0;
    if (!(ss >> utc >> snr >> dt >> freq >> sync)) return false;

    if (utc.size() != 6 && utc.size() != 4) return false;
    for (char c : utc) if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    if (utc.size() == 4) utc += "00";

    std::string msg;
    std::getline(ss, msg);
    const bool lowConf = msg.find('?') != std::string::npos;  // jt9 appends '?' when qual < 3
    size_t b = msg.find_first_not_of(" \t");
    msg = (b == std::string::npos) ? "" : msg.substr(b);

    if (size_t dbl = msg.find("  "); dbl != std::string::npos) msg = msg.substr(0, dbl);
    size_t e = msg.find_last_not_of(" \t\r\n");
    msg = (e == std::string::npos) ? "" : msg.substr(0, e + 1);
    if (msg.empty()) return false;

    d.utc = utc; d.snr = snr; d.dt = dt; d.freq = freq; d.mode = mode; d.message = msg;
    d.lowConf = lowConf;
    return true;
}

bool DecodeEngine::startJt9d() {
    if (jt9dPid_ > 0) return true;
    if (jt9dPath_.empty()) return false;

    int inpipe[2], outpipe[2];
    if (::pipe(inpipe) != 0) return false;
    if (::pipe(outpipe) != 0) { ::close(inpipe[0]); ::close(inpipe[1]); return false; }

    const std::string key = "ft8web_" + std::to_string(::getpid());
    const pid_t pid = ::fork();
    if (pid < 0) {
        ::close(inpipe[0]); ::close(inpipe[1]);
        ::close(outpipe[0]); ::close(outpipe[1]);
        return false;
    }
    if (pid == 0) {
        ::dup2(inpipe[0], STDIN_FILENO);
        ::dup2(outpipe[1], STDOUT_FILENO);
        ::close(inpipe[0]); ::close(inpipe[1]);
        ::close(outpipe[0]); ::close(outpipe[1]);
        ::execl(jt9dPath_.c_str(), "jt9d", key.c_str(), jt9_.c_str(),
                workDir_.c_str(), static_cast<char*>(nullptr));
        ::_exit(127);
    }
    ::close(inpipe[0]);
    ::close(outpipe[1]);
    jt9dIn_  = inpipe[1];
    jt9dOut_ = ::fdopen(outpipe[0], "r");
    jt9dPid_ = pid;
    if (!jt9dOut_) { stopJt9d(); return false; }
    return true;
}

void DecodeEngine::stopJt9d() {
    if (jt9dIn_ >= 0) { ::close(jt9dIn_); jt9dIn_ = -1; }
    if (jt9dOut_) { std::fclose(jt9dOut_); jt9dOut_ = nullptr; }
    if (jt9dPid_ > 0) {
        for (int i = 0; i < 20; ++i) {
            if (::waitpid(jt9dPid_, nullptr, WNOHANG) != 0) { jt9dPid_ = -1; return; }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        ::kill(jt9dPid_, SIGKILL);
        ::waitpid(jt9dPid_, nullptr, 0);
    }
    jt9dPid_ = -1;
}

bool DecodeEngine::decodeViaJt9d(const std::string& wav, int nutc, const std::string& band,
                                 const ModeSpec& spec, std::vector<Decode>& out) {
    std::string myCall, myGrid, hisCall, hisGrid;
    int prog = 0;
    {
        std::lock_guard<std::mutex> lk(stationMtx_);
        myCall = myCall_; myGrid = myGrid_;
        if (apCtx_) apCtx_(hisCall, hisGrid, prog);
    }
    const std::string mc = myCall.empty() ? "-" : myCall;
    const std::string mg = myGrid.empty() ? "-" : myGrid.substr(0, 4);
    const std::string hc = hisCall.empty() ? "-" : hisCall;
    const std::string hg = hisGrid.empty() ? "-" : hisGrid.substr(0, 4);

    const int nmode = spec.name == "FT4" ? 5 : spec.name == "FT2" ? 52 : 8;
    const int ntr   = static_cast<int>(spec.period);
    const long long dial = dialHz_ ? dialHz_() : 0;
    const int napwid = (dial > 0 && dial < 30000000) ? 5 : 75;

    char req[1600];
    const int len = std::snprintf(req, sizeof req, "%d %d %d %d 2 %d %d %s %s %s %s %s\n",
                                  nmode, ntr, nutc, rxFreq_.load(), prog, napwid,
                                  mc.c_str(), mg.c_str(), hc.c_str(), hg.c_str(), wav.c_str());
    if (len <= 0 || len >= static_cast<int>(sizeof req) ||
        ::write(jt9dIn_, req, static_cast<size_t>(len)) != len) {
        stopJt9d();
        return false;
    }

    char line[1024];
    bool finished = false;
    while (std::fgets(line, sizeof line, jt9dOut_)) {
        if (std::strncmp(line, "<DecodeFinished>", 16) == 0) { finished = true; break; }
        Decode d;
        if (parseLine(line, spec.name, d)) {
            d.band = band;
            out.push_back(std::move(d));
        }
    }
    if (!finished) { stopJt9d(); return false; }
    return true;
}

}
