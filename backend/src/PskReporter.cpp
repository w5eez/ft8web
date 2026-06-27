// SPDX-License-Identifier: GPL-3.0-or-later

#include "PskReporter.hpp"

#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <ctime>
#include <random>
#include <sstream>
#include <algorithm>

namespace ft8web {

namespace {

constexpr long long kUploadInterval = 300;
constexpr long long kQueryInterval  = 300;
constexpr uint32_t  kEnterprise     = 30351;

void put16(std::vector<uint8_t>& v, uint16_t x) { v.push_back(x >> 8); v.push_back(x & 0xff); }
void put32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(x >> 24); v.push_back((x >> 16) & 0xff); v.push_back((x >> 8) & 0xff); v.push_back(x & 0xff);
}
void putVarStr(std::vector<uint8_t>& v, const std::string& s) {
    const size_t n = std::min<size_t>(s.size(), 254);
    v.push_back(static_cast<uint8_t>(n));
    v.insert(v.end(), s.begin(), s.begin() + n);
}
void pad4(std::vector<uint8_t>& v, size_t setStart) {
    while ((v.size() - setStart) % 4) v.push_back(0);
}

bool plausibleCall(const std::string& c) {
    if (c.size() < 3 || c.size() > 12) return false;
    bool digit = false, alpha = false;
    for (char ch : c) {
        if (std::isdigit(static_cast<unsigned char>(ch))) digit = true;
        else if (std::isalpha(static_cast<unsigned char>(ch))) alpha = true;
        else if (ch != '/') return false;
    }
    return digit && alpha;
}

std::string xmlAttr(const std::string& tag, const std::string& name) {
    auto p = tag.find(name + "=\"");
    if (p == std::string::npos) return {};
    p += name.size() + 2;
    auto e = tag.find('"', p);
    return e == std::string::npos ? std::string{} : tag.substr(p, e - p);
}

}

std::string PskReporter::parseGrid(const std::string& msg) {
    std::istringstream ss(msg);
    std::string tok, last;
    while (ss >> tok) last = tok;
    if (last.size() != 4 || last == "RR73") return {};
    auto inR = [](char c) { return c >= 'A' && c <= 'R'; };
    if (inR(last[0]) && inR(last[1]) &&
        std::isdigit(static_cast<unsigned char>(last[2])) &&
        std::isdigit(static_cast<unsigned char>(last[3]))) return last;
    return {};
}

std::string PskReporter::parseSender(const std::string& msg) {
    std::vector<std::string> t;
    std::istringstream ss(msg);
    std::string tok;
    while (ss >> tok) t.push_back(tok);
    if (t.empty()) return {};
    if (t[0] == "CQ") {
        size_t end = t.size();
        if (end > 1 && !parseGrid(msg).empty()) end--;
        return t[end - 1];
    }
    return t.size() >= 2 ? t[1] : t[0];
}

PskReporter::PskReporter() {
    std::random_device rd;
    obsDomain_ = rd();
    thread_ = std::thread(&PskReporter::loop, this);
}

PskReporter::~PskReporter() {
    run_.store(false);
    if (thread_.joinable()) thread_.join();
}

void PskReporter::configure(const std::string& callsign, const std::string& grid, bool enabled) {
    std::lock_guard<std::mutex> l(cfgMtx_);
    call_ = callsign;
    grid_ = grid;
    enabled_.store(enabled && !callsign.empty());
    if (!enabled_.load()) {
        { std::lock_guard<std::mutex> r(repMtx_); reports_.clear(); }

        std::lock_guard<std::mutex> b(bufMtx_);
        buf_.clear();
    }
}

bool PskReporter::gated() const {
    std::lock_guard<std::mutex> l(cfgMtx_);
    return enabled_.load() && !call_.empty();
}

void PskReporter::addDecode(const std::string& message, int snr, int audioOffsetHz,
                            const std::string& mode, long long dialHz) {
    if (!gated() || dialHz <= 0) return;
    std::string call, locator;
    if (mode == "WSPR") {

        std::istringstream ss(message);
        ss >> call >> locator;
        if (locator.size() != 4 || !std::isalpha(static_cast<unsigned char>(locator[0])))
            locator.clear();
    } else {
        call = parseSender(message);
        locator = parseGrid(message);
    }
    if (call.find('<') != std::string::npos || !plausibleCall(call)) return;

    {
        std::string upper = call;
        std::transform(upper.begin(), upper.end(), upper.begin(),
                       [](unsigned char c) { return std::toupper(c); });
        std::lock_guard<std::mutex> l(cfgMtx_);
        if (!call_.empty() && upper == call_) return;
    }

    Spot s;
    s.call    = call;
    s.locator = locator;
    s.mode    = mode;
    s.freq    = dialHz + audioOffsetHz;
    s.snr     = snr;
    s.t       = static_cast<long long>(std::time(nullptr));

    const std::string key = call + "|" + mode + "|" + std::to_string(s.freq / 1000);
    std::lock_guard<std::mutex> l(bufMtx_);
    auto it = buf_.find(key);
    if (it != buf_.end()) {
        if (snr > it->second.snr) it->second = std::move(s);
    } else if (buf_.size() <= 500) {
        buf_[key] = std::move(s);
    }
}

void PskReporter::loop() {
    while (run_.load()) {
        for (int i = 0; i < 100 && run_.load(); ++i)
            usleep(100000);
        if (!run_.load()) break;
        if (!gated()) continue;
        const long long now = std::time(nullptr);
        if (now - lastUploadDone_.load() >= kUploadInterval) uploadBatch();
        if (now - lastQueryDone_.load()  >= kQueryInterval)  runQuery();
    }
}

std::vector<uint8_t> PskReporter::buildPacket(const std::vector<Spot>& spots) {
    std::string call, grid;
    { std::lock_guard<std::mutex> l(cfgMtx_); call = call_; grid = grid_; }

    std::vector<uint8_t> p;

    put16(p, 0x000A); put16(p, 0);
    put32(p, static_cast<uint32_t>(std::time(nullptr)));
    put32(p, seq_);
    put32(p, obsDomain_);

    auto entField = [&](uint16_t id) { put16(p, 0x8000 | id); put16(p, 0xFFFF); put32(p, kEnterprise); };

    size_t s0 = p.size();
    put16(p, 2); put16(p, 0);
    put16(p, 0x9992); put16(p, 3);
    entField(0x02); entField(0x04); entField(0x08);
    p[s0 + 2] = ((p.size() - s0) >> 8) & 0xff; p[s0 + 3] = (p.size() - s0) & 0xff;

    s0 = p.size();
    put16(p, 2); put16(p, 0);
    put16(p, 0x9993); put16(p, 7);
    entField(0x01);
    put16(p, 0x8005); put16(p, 4); put32(p, kEnterprise);
    put16(p, 0x8006); put16(p, 1); put32(p, kEnterprise);
    entField(0x0A);
    entField(0x03);
    put16(p, 0x800B); put16(p, 1); put32(p, kEnterprise);
    put16(p, 0x0096); put16(p, 4);
    p[s0 + 2] = ((p.size() - s0) >> 8) & 0xff; p[s0 + 3] = (p.size() - s0) & 0xff;

    s0 = p.size();
    put16(p, 0x9992); put16(p, 0);
    putVarStr(p, call);
    putVarStr(p, grid);
    putVarStr(p, "ft8web 0.1");
    pad4(p, s0);
    p[s0 + 2] = ((p.size() - s0) >> 8) & 0xff; p[s0 + 3] = (p.size() - s0) & 0xff;

    s0 = p.size();
    put16(p, 0x9993); put16(p, 0);
    for (const auto& s : spots) {
        putVarStr(p, s.call);
        put32(p, static_cast<uint32_t>(s.freq));
        p.push_back(static_cast<uint8_t>(static_cast<int8_t>(s.snr)));
        putVarStr(p, s.mode);
        putVarStr(p, s.locator);
        p.push_back(1);
        put32(p, static_cast<uint32_t>(s.t));
    }
    pad4(p, s0);
    p[s0 + 2] = ((p.size() - s0) >> 8) & 0xff; p[s0 + 3] = (p.size() - s0) & 0xff;

    p[2] = (p.size() >> 8) & 0xff; p[3] = p.size() & 0xff;
    seq_ += static_cast<uint32_t>(spots.size());
    return p;
}

void PskReporter::uploadBatch() {
    const long long now = std::time(nullptr);
    std::vector<Spot> spots;
    {
        std::lock_guard<std::mutex> l(bufMtx_);
        for (auto& kv : buf_)
            if (now - kv.second.t <= kUploadInterval + 60)
                spots.push_back(kv.second);
        buf_.clear();
    }
    lastUploadDone_.store(now);
    if (spots.empty()) return;

    struct addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    struct addrinfo* res = nullptr;
    if (::getaddrinfo("report.pskreporter.info", "4739", &hints, &res) != 0 || !res) return;

    int fd = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd >= 0) {

        for (size_t i = 0; i < spots.size(); i += 40) {
            std::vector<Spot> chunk(spots.begin() + i,
                                    spots.begin() + std::min(spots.size(), i + 40));
            auto pkt = buildPacket(chunk);
            ::sendto(fd, pkt.data(), pkt.size(), 0, res->ai_addr, res->ai_addrlen);
        }
        ::close(fd);
        std::fprintf(stderr, "[pskreporter] uploaded %zu spots\n", spots.size());
    }
    ::freeaddrinfo(res);
}

void PskReporter::runQuery() {
    std::string call;
    { std::lock_guard<std::mutex> l(cfgMtx_); call = call_; }
    lastQueryDone_.store(std::time(nullptr));

    std::string safe;
    for (char c : call)
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '/') safe += c;
    if (safe.empty()) return;

    std::string cmd =
        "curl -s --compressed --max-time 25 "
        "'https://retrieve.pskreporter.info/query?senderCallsign=" + safe +
        "&flowStartSeconds=-21600&rronly=true&appcontact=ft8web' 2>/dev/null";
    FILE* pp = ::popen(cmd.c_str(), "r");
    if (!pp) return;
    std::string xml;
    char tmp[4096];
    size_t n;
    while ((n = std::fread(tmp, 1, sizeof tmp, pp)) > 0) {
        xml.append(tmp, n);
        if (xml.size() > 4u * 1024 * 1024) break;
    }
    ::pclose(pp);
    if (xml.find("receptionReport") == std::string::npos) return;

    std::vector<RxReport> out;
    size_t pos = 0;
    while ((pos = xml.find("<receptionReport ", pos)) != std::string::npos) {
        auto end = xml.find('>', pos);
        if (end == std::string::npos) break;
        const std::string tag = xml.substr(pos, end - pos);
        pos = end;
        RxReport r;
        r.receiver = xmlAttr(tag, "receiverCallsign");
        r.locator  = xmlAttr(tag, "receiverLocator");
        r.mode     = xmlAttr(tag, "mode");
        const std::string f = xmlAttr(tag, "frequency");
        const std::string s = xmlAttr(tag, "sNR");
        const std::string t = xmlAttr(tag, "flowStartSeconds");
        r.freq = f.empty() ? 0 : std::atoll(f.c_str());
        r.snr  = s.empty() ? 0 : std::atoi(s.c_str());
        r.t    = t.empty() ? 0 : std::atoll(t.c_str());
        if (!r.receiver.empty()) out.push_back(std::move(r));
    }
    {
        std::lock_guard<std::mutex> l(repMtx_);
        reports_ = std::move(out);
    }
    std::lock_guard<std::mutex> l(repMtx_);
    std::fprintf(stderr, "[pskreporter] query ok: %zu reception reports\n", reports_.size());
}

std::vector<RxReport> PskReporter::reports() const {
    std::lock_guard<std::mutex> l(repMtx_);
    return reports_;
}

}
