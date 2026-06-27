// SPDX-License-Identifier: GPL-3.0-or-later

#include "WorkedLog.hpp"
#include "LogStore.hpp"

#include <fstream>
#include <sstream>
#include <cstdio>
#include <cctype>
#include <chrono>
#include <sys/stat.h>

namespace ft8web {

namespace {

long long nowSec() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string upper(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

bool isGrid4(const std::string& s) {
    return s.size() == 4 && s[0] >= 'A' && s[0] <= 'R' && s[1] >= 'A' && s[1] <= 'R'
        && std::isdigit(static_cast<unsigned char>(s[2])) && std::isdigit(static_cast<unsigned char>(s[3]));
}

std::string shellQuote(const std::string& s) {
    std::string o = "'";
    for (char c : s) { if (c == '\'') o += "'\\''"; else o += c; }
    o += "'";
    return o;
}

std::string runPopen(const std::string& cmd) {
    std::string out;
    FILE* pp = ::popen(cmd.c_str(), "r");
    if (!pp) return out;
    char buf[8192];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, pp)) > 0) out.append(buf, n);
    ::pclose(pp);
    return out;
}

std::string htmlDecode(const std::string& s) {
    std::string o;
    o.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        if (s[i] == '&') {
            if (s.compare(i, 4, "&lt;")  == 0) { o += '<';  i += 4; continue; }
            if (s.compare(i, 4, "&gt;")  == 0) { o += '>';  i += 4; continue; }
            if (s.compare(i, 5, "&amp;") == 0) { o += '&';  i += 5; continue; }
            if (s.compare(i, 6, "&quot;")== 0) { o += '"';  i += 6; continue; }
            if (s.compare(i, 6, "&apos;")== 0) { o += '\''; i += 6; continue; }
            if (s.compare(i, 5, "&#39;") == 0) { o += '\''; i += 5; continue; }
        }
        o += s[i++];
    }
    return o;
}

std::string field(const QsoFields& r, const char* k) {
    auto it = r.find(k);
    return it == r.end() ? std::string() : it->second;
}

}

WorkedLog::WorkedLog(std::string cacheFile) : cacheFile_(std::move(cacheFile)) {
    thread_ = std::thread(&WorkedLog::loop, this);
}

WorkedLog::~WorkedLog() {
    run_.store(false);
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
}

void WorkedLog::configure(const std::string& qrzKey) {
    bool changed;
    { std::lock_guard<std::mutex> lk(mtx_); changed = (qrzKey != qrzKey_); qrzKey_ = qrzKey; }
    if (changed && !qrzKey.empty()) requestSync();
}

void WorkedLog::requestSync() {
    syncReq_.store(true);
    cv_.notify_all();
}

void WorkedLog::rebuild(const std::string& adif) {
    auto records = LogStore::parseAdifRecords(adif);
    std::set<std::string> calls, slots, grids;
    for (auto& r : records) {
        const std::string call = upper(field(r, "call"));
        if (call.empty()) continue;
        calls.insert(call);
        const std::string band = upper(field(r, "band"));
        const std::string sub  = field(r, "submode"), mode = field(r, "mode");
        const std::string m    = upper(sub.empty() ? mode : sub);
        if (!band.empty() && !m.empty()) slots.insert(call + "|" + band + "|" + m);
        std::string g = upper(field(r, "gridsquare"));
        if (g.size() > 4) g = g.substr(0, 4);
        if (isGrid4(g)) grids.insert(g);
    }
    std::lock_guard<std::mutex> lk(mtx_);
    calls_ = std::move(calls);
    slots_ = std::move(slots);
    grids_ = std::move(grids);
    qsos_  = static_cast<int>(records.size());
}

bool WorkedLog::fetchAndRebuild() {
    std::string key;
    { std::lock_guard<std::mutex> lk(mtx_); key = qrzKey_; }
    if (key.empty()) { std::lock_guard<std::mutex> lk(mtx_); error_ = "no QRZ logbook key"; return false; }

    const std::string cmd = "curl -s --max-time 60 --data " + shellQuote("KEY=" + key) +
                            " --data ACTION=FETCH https://logbook.qrz.com/api 2>/dev/null";
    const std::string resp = runPopen(cmd);
    if (resp.empty()) { std::lock_guard<std::mutex> lk(mtx_); error_ = "QRZ fetch: no response"; return false; }
    if (resp.find("RESULT=OK") == std::string::npos) {
        std::string reason = "QRZ fetch failed";
        auto rp = resp.find("REASON=");
        if (rp != std::string::npos) {
            auto end = resp.find('&', rp);
            reason = "QRZ: " + resp.substr(rp + 7, (end == std::string::npos ? resp.size() : end) - (rp + 7));
        }
        std::lock_guard<std::mutex> lk(mtx_); error_ = reason; return false;
    }
    auto ap = resp.find("ADIF=");
    if (ap == std::string::npos) { std::lock_guard<std::mutex> lk(mtx_); error_ = "QRZ: no ADIF in response"; return false; }

    const std::string adif = htmlDecode(resp.substr(ap + 5));
    rebuild(adif);
    { std::lock_guard<std::mutex> lk(mtx_); syncedSec_ = nowSec(); error_.clear(); }

    std::ofstream f(cacheFile_, std::ios::trunc);
    if (f) f << adif;
    return true;
}

void WorkedLog::loadCache() {
    std::ifstream f(cacheFile_);
    if (!f) return;
    std::stringstream ss;
    ss << f.rdbuf();
    rebuild(ss.str());
    struct stat st {};
    if (::stat(cacheFile_.c_str(), &st) == 0) {
        std::lock_guard<std::mutex> lk(mtx_);
        syncedSec_ = static_cast<long long>(st.st_mtime);
    }
}

void WorkedLog::loop() {
    loadCache();
    while (run_.load()) {
        bool hasKey;
        { std::lock_guard<std::mutex> lk(mtx_); hasKey = !qrzKey_.empty(); }
        if (hasKey) {
            syncing_.store(true);
            fetchAndRebuild();
            syncing_.store(false);
        }
        std::unique_lock<std::mutex> lk(cvMtx_);
        cv_.wait_for(lk, std::chrono::hours(4),
                     [this] { return !run_.load() || syncReq_.exchange(false); });
    }
}

WorkedLog::Snapshot WorkedLog::snapshot() const {
    std::lock_guard<std::mutex> lk(mtx_);
    Snapshot s;
    s.calls.assign(calls_.begin(), calls_.end());
    s.slots.assign(slots_.begin(), slots_.end());
    s.grids.assign(grids_.begin(), grids_.end());
    s.qsos      = qsos_;
    s.syncedSec = syncedSec_;
    s.syncing   = syncing_.load();
    s.haveKey   = !qrzKey_.empty();
    s.error     = error_;
    return s;
}

}
