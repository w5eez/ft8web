// SPDX-License-Identifier: GPL-3.0-or-later
#include "QsoEngine.hpp"
#include "DecodeEngine.hpp"
#include "FreqPlan.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <sstream>

namespace ft8web {
namespace {

std::string upper(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}
std::string stripAngle(std::string s) {
    s.erase(std::remove(s.begin(), s.end(), '<'), s.end());
    s.erase(std::remove(s.begin(), s.end(), '>'), s.end());
    return s;
}
std::vector<std::string> tokens(const std::string& msg) {
    std::vector<std::string> out;
    std::istringstream is(msg);
    std::string t;
    while (is >> t) out.push_back(stripAngle(t));
    return out;
}
bool isGrid4(const std::string& t) {
    if (t.size() != 4 || t == "RR73") return false;
    return t[0] >= 'A' && t[0] <= 'R' && t[1] >= 'A' && t[1] <= 'R' &&
           std::isdigit(static_cast<unsigned char>(t[2])) &&
           std::isdigit(static_cast<unsigned char>(t[3]));
}
bool isSignedRpt(const std::string& t) {
    if (t.size() < 2 || (t[0] != '+' && t[0] != '-')) return false;
    return std::all_of(t.begin() + 1, t.end(),
                       [](char c) { return std::isdigit(static_cast<unsigned char>(c)); });
}
bool isFdExch(const std::string& t) {
    if (t.size() < 2) return false;
    if (t.back() < 'A' || t.back() > 'F') return false;
    return std::all_of(t.begin(), t.end() - 1,
                       [](char c) { return std::isdigit(static_cast<unsigned char>(c)); });
}
bool hasCallChars(const std::string& t) {
    return std::any_of(t.begin(), t.end(), [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)); });
}
bool isGridLoose(const std::string& t) {
    if (t.size() != 4 && t.size() != 6) return false;
    auto AR = [](char c) { return c >= 'A' && c <= 'R'; };
    auto AX = [](char c) { return c >= 'A' && c <= 'X'; };
    auto D  = [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; };
    if (!AR(t[0]) || !AR(t[1]) || !D(t[2]) || !D(t[3])) return false;
    if (t.size() == 6 && (!AX(t[4]) || !AX(t[5]))) return false;
    return true;
}
bool isReportTok(const std::string& t) {
    size_t i = 0;
    if (i < t.size() && t[i] == 'R') ++i;
    if (i < t.size() && (t[i] == '+' || t[i] == '-')) ++i;
    if (i >= t.size()) return false;
    return std::all_of(t.begin() + static_cast<long>(i), t.end(),
                       [](char c) { return std::isdigit(static_cast<unsigned char>(c)); });
}
bool looksCall(const std::string& t) {

    const bool hasDigit = std::any_of(t.begin(), t.end(), [](char c) {
        return std::isdigit(static_cast<unsigned char>(c)); });
    const bool hasAlpha = std::any_of(t.begin(), t.end(), [](char c) {
        return std::isalpha(static_cast<unsigned char>(c)); });
    return hasDigit && hasAlpha && !isGridLoose(t) && !isReportTok(t);
}
std::string unhash(std::string t) {
    if (t.size() >= 2 && t.front() == '<' && t.back() == '>') return t.substr(1, t.size() - 2);
    return t;
}
std::string baseCall(const std::string& s) {
    std::string best;
    size_t i = 0;
    while (true) {
        const size_t j = s.find('/', i);
        const std::string seg = s.substr(i, (j == std::string::npos ? s.size() : j) - i);
        const bool hasD = std::any_of(seg.begin(), seg.end(),
            [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; });
        const bool hasA = std::any_of(seg.begin(), seg.end(),
            [](char c) { return std::isalpha(static_cast<unsigned char>(c)) != 0; });
        if (hasD && hasA && seg.size() > best.size()) best = seg;
        if (j == std::string::npos) break;
        i = j + 1;
    }
    return best.empty() ? s : best;
}
double periodFor(const std::string& mode) {
    if (const auto* m = DecodeEngine::findMode(mode)) return m->period;
    return 15.0;
}
std::string rstrip(std::string s) {
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

}

std::string QsoEngine::fmtRpt(int snr) {
    int r = std::max(-30, std::min(30, snr));
    char buf[8];
    std::snprintf(buf, sizeof buf, "%c%02d", r < 0 ? '-' : '+', std::abs(r));
    return buf;
}

int QsoEngine::replyTx(int stage) {
    return stage <= 0 ? 1 : std::min(stage + 1, 5);
}

QsoEngine::Stage QsoEngine::classifyStage(const std::vector<std::string>& toks, bool fd) {

    const std::string t2 = toks.size() > 2 ? upper(toks[2]) : "";
    const std::string t3 = toks.size() > 3 ? upper(toks[3]) : "";
    const bool grid = isGrid4(t2);
    if (fd) {
        if (t2 == "R" && isFdExch(t3)) {
            const std::string t4 = toks.size() > 4 ? upper(toks[4]) : "";
            return {3, rstrip(t3 + " " + t4)};
        }
        if (isFdExch(t2)) return {2, rstrip(t2 + " " + t3)};
        if (t2 == "RR73" || t2 == "RRR") return {4, ""};
        if (t2 == "73") return {5, ""};
        if (grid || t2.empty()) return {1, ""};
        return {0, ""};
    }
    if (grid || t2.empty()) return {1, ""};
    if (t2.size() > 1 && t2[0] == 'R' && isSignedRpt(t2.substr(1))) return {3, t2.substr(1)};
    if (isSignedRpt(t2)) return {2, t2};
    if (t2 == "RR73" || t2 == "RRR") return {4, ""};
    if (t2 == "73") return {5, ""};
    return {0, ""};
}

bool QsoEngine::decodeIsEvenUtc(const std::string& utc, double periodS, bool& evenOut) {

    if (utc.size() != 6 ||
        !std::all_of(utc.begin(), utc.end(),
                     [](char c) { return std::isdigit(static_cast<unsigned char>(c)); }))
        return false;
    const int sec = (utc[0]-'0')*36000 + (utc[1]-'0')*3600 +
                    (utc[2]-'0')*600  + (utc[3]-'0')*60 +
                    (utc[4]-'0')*10   + (utc[5]-'0');
    evenOut = (static_cast<long long>(std::llround(sec / periodS)) % 2) == 0;
    return true;
}

long long QsoEngine::epochFromUtc(const std::string& utc, long long nowMs) {

    if (utc.size() != 6) return nowMs;
    const std::time_t nowS = static_cast<std::time_t>(nowMs / 1000);
    std::tm g{};
    gmtime_r(&nowS, &g);
    g.tm_hour = (utc[0]-'0')*10 + (utc[1]-'0');
    g.tm_min  = (utc[2]-'0')*10 + (utc[3]-'0');
    g.tm_sec  = (utc[4]-'0')*10 + (utc[5]-'0');
    long long t = static_cast<long long>(timegm(&g)) * 1000;
    if (t - nowMs >  12LL*3600*1000) t -= 24LL*3600*1000;
    if (nowMs - t >  12LL*3600*1000) t += 24LL*3600*1000;
    return t;
}

QsoEngine::Sender QsoEngine::parseSender(const std::string& message) {

    auto toks = tokens(message);
    for (auto& t : toks) t = upper(t);
    Sender s;
    if (toks.empty()) return s;
    if (toks[0] == "CQ") {

        for (size_t i = 1; i < toks.size(); ++i) {
            if (looksCall(toks[i])) {
                s.call = unhash(toks[i]);
                if (i + 1 < toks.size() && isGrid4(toks[i + 1])) s.grid = toks[i + 1];
                break;
            }
        }
        return s;
    }

    if (toks.size() >= 2) {
        s.call = unhash(toks[1]);
        if (toks.size() >= 3 && isGrid4(toks[2])) s.grid = toks[2];
    }
    return s;
}

std::vector<std::string> QsoEngine::stdMessages(const QsoConfigSnapshot& cfg,
                                                const std::string& dxCall, int report) {

    const std::string me = cfg.callsign.empty() ? "NOCALL" : cfg.callsign;
    const std::string grid4 = cfg.grid.substr(0, std::min<size_t>(4, cfg.grid.size()));
    const std::string dx = dxCall.empty() ? "?" : dxCall;
    const std::string rpt = fmtRpt(report);
    const std::string ack = "RR73";
    if (cfg.opMode == "fd") {
        std::string exch = rstrip(cfg.fdClass + " " + cfg.fdSection);
        if (exch.empty()) exch = "1D ?";
        return {rstrip(dx + " " + me + " " + grid4),
                dx + " " + me + " " + exch,
                dx + " " + me + " R " + exch,
                dx + " " + me + " " + ack,
                dx + " " + me + " 73",
                rstrip("CQ FD " + me + " " + grid4)};
    }
    const std::string cq = cfg.opMode == "pota" ? "CQ POTA" : "CQ";
    if (dx.find('/') != std::string::npos) {
        return {dx + " <" + me + ">",
                "<" + dx + "> " + me + " " + rpt,
                "<" + dx + "> " + me + " R" + rpt,
                dx + " <" + me + "> " + ack,
                dx + " <" + me + "> 73",
                rstrip(cq + " " + me + " " + grid4)};
    }
    return {rstrip(dx + " " + me + " " + grid4),
            dx + " " + me + " " + rpt,
            dx + " " + me + " R" + rpt,
            dx + " " + me + " " + ack,
            dx + " " + me + " 73",
            rstrip(cq + " " + me + " " + grid4)};
}

std::string QsoEngine::wsprMessage(const std::string& callsign, const std::string& grid,
                                   double watts) {

    static const int valid[] = {0,3,7,10,13,17,20,23,27,30,33,37,40,43,47,50,53,57,60};
    const int dbm = static_cast<int>(std::lround(10.0 * std::log10(std::max(0.001, watts) * 1000.0)));
    int best = 0;
    for (int v : valid) if (std::abs(v - dbm) < std::abs(best - dbm)) best = v;
    const std::string me = callsign.empty() ? "NOCALL" : callsign;
    return me + " " + grid.substr(0, std::min<size_t>(4, grid.size())) + " " + std::to_string(best);
}

QsoEngine::QsoEngine(QsoHooks h) : hooks_(std::move(h)) {}

long long QsoEngine::nowMs() const {
    if (nowOverride) return nowOverride();
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

void QsoEngine::applyConfig(const QsoConfigSnapshot& c) {
    std::vector<std::function<void()>> fx;
    {
        std::lock_guard<std::mutex> l(mtx_);
        cfg_ = c;
        st_.homeEven = c.homeEven;
        syncTxLocked(fx);
        pushLocked(fx);
    }
    for (auto& f : fx) f();
}

void QsoEngine::onModeChange(const std::string& mode) {
    std::vector<std::function<void()>> fx;
    {
        std::lock_guard<std::mutex> l(mtx_);
        if (mode == mode_) return;
        mode_ = mode;

        abortLocked(fx);
    }
    for (auto& f : fx) f();
}

void QsoEngine::clientsAlive(bool alive) {
    std::vector<std::function<void()>> fx;
    {
        std::lock_guard<std::mutex> l(mtx_);
        st_.clientsAlive = alive;
    }
    for (auto& f : fx) f();
}

bool QsoEngine::qsoActive() const {
    std::lock_guard<std::mutex> l(mtx_);
    return !st_.dxCall.empty();
}

bool QsoEngine::qsoInProgress() const {
    std::lock_guard<std::mutex> l(mtx_);
    return !st_.dxCall.empty() && !st_.complete;
}

bool QsoEngine::effectiveEven() const {
    std::lock_guard<std::mutex> l(mtx_);
    return st_.flipped ? st_.flipTo : st_.homeEven;
}

void QsoEngine::apContext(std::string& hisCall, std::string& hisGrid, int& qsoProgress) const {
    std::lock_guard<std::mutex> l(mtx_);
    hisCall = st_.dxCall;
    hisGrid = st_.dxGrid;
    qsoProgress = st_.dxCall.empty() ? 0
                : (st_.selectedTx >= 1 && st_.selectedTx <= 5 ? st_.selectedTx : 0);
}

bool QsoEngine::skipTxSlot(long long slot) {
    std::lock_guard<std::mutex> l(mtx_);
    if (!cfg_.cqSkip) return false;
    if (!st_.dxCall.empty() || st_.selectedTx != 6) { st_.cqRefSlot = -1; return false; }
    if (st_.cqRefSlot < 0) st_.cqRefSlot = slot;
    return ((slot - st_.cqRefSlot) / 2) % 2 == 1;
}

void QsoEngine::resetProgressLocked(long long t) {
    st_.attempts = 0;
    st_.lastStage = -1;
    st_.lastUtc.clear();
    st_.lastHeardMs = t;
    st_.rcvdReport.clear();
    st_.logged = false;
    st_.completedEpoch = 0;
    st_.txedThisQso = false;
    st_.startedEpoch = t;
}

void QsoEngine::setRxOffsetLocked(int hz, std::vector<std::function<void()>>& fx) {
    if (hz <= 0 || hz == st_.rxOffset) return;
    st_.rxOffset = hz;
    if (hooks_.setRxOffset) fx.push_back([h = hooks_.setRxOffset, hz] { h(hz); });
}

void QsoEngine::adoptOppositeSlotLocked(const Decode& d) {
    bool theirEven = false;
    if (!decodeIsEvenUtc(d.utc, periodFor(d.mode), theirEven)) return;
    const bool want = !theirEven;
    st_.flipped = (want != st_.homeEven);
    st_.flipTo = want;
}

void QsoEngine::idleAfterQsoLocked(std::vector<std::function<void()>>& fx) {

    if (cfg_.autoCq && st_.clientsAlive) return;
    if (hooks_.txDisarm) fx.push_back(hooks_.txDisarm);
}

void QsoEngine::abortLocked(std::vector<std::function<void()>>& fx) {
    const bool wasComplete = st_.complete;
    st_.complete = false;
    resetProgressLocked(0);
    st_.lastHeardMs = 0;
    st_.dxCall.clear(); st_.dxGrid.clear(); st_.report = 0;
    st_.selectedTx = 6;
    st_.flipped = false;
    setRxOffsetLocked(1500, fx);
    syncTxLocked(fx);
    idleAfterQsoLocked(fx);
    st_.lastEvent = wasComplete ? "completed" : "aborted";
    pushLocked(fx);
}

bool QsoEngine::trackStageLocked(int stage, const std::string& utc,
                                 std::vector<std::function<void()>>& fx) {

    if (utc == st_.lastUtc) return true;
    st_.lastUtc = utc;
    if (stage == st_.lastStage) {
        if (++st_.attempts >= cfg_.retries) { abortLocked(fx); return false; }
    } else {
        st_.lastStage = stage;
        st_.attempts = 1;
    }
    return true;
}

std::vector<std::string> QsoEngine::messagesLocked() const {
    return stdMessages(cfg_, st_.dxCall, st_.report);
}

void QsoEngine::syncTxLocked(std::vector<std::function<void()>>& fx) {

    if (!hooks_.txArmed || !hooks_.txArmed()) return;
    std::string msg;
    if (mode_ == "WSPR") {
        msg = wsprMessage(cfg_.callsign, cfg_.grid, hooks_.txPowerW ? hooks_.txPowerW() : 10.0);
    } else {
        auto m = messagesLocked();
        msg = m[static_cast<size_t>(std::max(1, std::min(6, st_.selectedTx)) - 1)];
    }
    if (hooks_.txUpdate) fx.push_back([h = hooks_.txUpdate, msg] { h(msg); });
}

void QsoEngine::logQsoLocked(std::vector<std::function<void()>>& fx) {
    if (st_.logged) return;
    if (!st_.txedThisQso) return;
    if (st_.dxCall.empty()) return;
    st_.logged = true;
    long long offMs = st_.completedEpoch ? st_.completedEpoch : nowMs();
    const long long onMs  = st_.startedEpoch ? st_.startedEpoch : offMs;
    if (offMs < onMs) offMs = onMs;
    auto fmt = [](long long ms, char* date, char* tim) {
        const std::time_t s = static_cast<std::time_t>(ms / 1000);
        std::tm g{};
        gmtime_r(&s, &g);
        std::snprintf(date, 16, "%04d%02d%02d", g.tm_year + 1900, g.tm_mon + 1, g.tm_mday);
        std::snprintf(tim, 16, "%02d%02d%02d", g.tm_hour, g.tm_min, g.tm_sec);
    };
    char dateOn[16], timOn[16], dateOff[16], timOff[16];
    fmt(onMs, dateOn, timOn);
    fmt(offMs, dateOff, timOff);
    const long long dial = hooks_.dialHz ? hooks_.dialHz() : 0;
    const int txo = hooks_.txOffsetHz ? hooks_.txOffsetHz() : 0;
    char freq[24] = "";
    if (dial) std::snprintf(freq, sizeof freq, "%.6f", (dial + txo) / 1e6);
    std::map<std::string, std::string> q{
        {"call",          upper(st_.dxCall)},
        {"qso_date",      dateOn},
        {"time_on",       timOn},
        {"qso_date_off",  dateOff},
        {"time_off",      timOff},
        {"band",          FreqPlan::bandFor(dial)},
        {"freq",          freq},
        {"mode",          mode_},
        {"rst_sent",      fmtRpt(st_.report)},
        {"rst_rcvd",      st_.rcvdReport},
        {"gridsquare",    upper(st_.dxGrid)},
        {"my_gridsquare", upper(st_.myGridLocked.empty() ? cfg_.grid : st_.myGridLocked)},
    };
    if (hooks_.logQso) fx.push_back([h = hooks_.logQso, q] { h(q); });
}

void QsoEngine::callDecode(const Decode& d) {
    std::vector<std::function<void()>> fx;
    {
        std::lock_guard<std::mutex> l(mtx_);
        auto s = parseSender(d.message);
        if (s.call.empty() || !hasCallChars(s.call)) return;
        if (!cfg_.callsign.empty() && s.call == upper(cfg_.callsign)) return;
        st_.complete = false;
        resetProgressLocked(nowMs());
        st_.lastPeriodS = periodFor(d.mode);
        st_.dxCall = s.call;
        st_.dxGrid = s.grid;
        st_.report = d.snr;
        st_.myGridLocked = cfg_.grid;
        auto toks = tokens(d.message);
        for (auto& t : toks) t = upper(t);
        const bool fd = cfg_.opMode == "fd";
        if (!cfg_.callsign.empty() && !toks.empty() && toks[0] == upper(cfg_.callsign)) {
            auto cls = classifyStage(toks, fd);
            if (cls.stage > 0) {
                if (!cls.rcvd.empty()) st_.rcvdReport = cls.rcvd;
                st_.selectedTx = replyTx(cls.stage);
                if (cls.stage >= 3) {
                    st_.complete = true;
                    st_.completedEpoch = epochFromUtc(d.utc, nowMs());
                    st_.txedThisQso = true;
                    logQsoLocked(fx);
                }
            } else st_.selectedTx = 1;
        } else st_.selectedTx = 1;
        adoptOppositeSlotLocked(d);
        setRxOffsetLocked(d.freq, fx);
        syncTxLocked(fx);
        pushLocked(fx);
    }
    for (auto& f : fx) f();
}

void QsoEngine::onDecode(const Decode& d) {
    std::vector<std::function<void()>> fx;
    {
        std::lock_guard<std::mutex> l(mtx_);
        onDecodeLocked(d, fx);
    }
    for (auto& f : fx) f();
}

void QsoEngine::onTxDone() {
    std::vector<std::function<void()>> fx;
    {
        std::lock_guard<std::mutex> l(mtx_);

        if (!st_.dxCall.empty()) st_.txedThisQso = true;
        if (st_.complete && st_.selectedTx == 5) {
            abortLocked(fx);
        } else if (st_.dxCall.empty() && st_.selectedTx == 6) {
            if (cfg_.maxCqs > 0 && ++st_.cqCount >= cfg_.maxCqs) {
                st_.cqCount = 0;
                if (hooks_.txDisarm) fx.push_back(hooks_.txDisarm);
            }
        } else {
            st_.cqCount = 0;
        }
    }
    for (auto& f : fx) f();
}

void QsoEngine::onDecodeLocked(const Decode& d, std::vector<std::function<void()>>& fx) {

    if (d.mode == "WSPR") return;
    if (d.lowConf) return;   // never auto-start/advance a QSO off a low-confidence decode
    if (cfg_.callsign.empty()) return;
    auto toks = tokens(d.message);
    for (auto& t : toks) t = upper(t);
    const std::string my = upper(cfg_.callsign);
    if (toks.size() < 2 || toks[0] != my) return;

    const std::string sender = unhash(toks[1]);
    if (!hasCallChars(sender)) return;
    const std::string cur = upper(st_.dxCall);
    const long long t = epochFromUtc(d.utc, nowMs());
    const bool samePartner = !cur.empty() && baseCall(cur) == baseCall(sender);
    if (!cur.empty() && !samePartner) {
        if (!st_.complete) return;
        if (st_.completedEpoch && t <= st_.completedEpoch) return;

        if (!(cfg_.autoCq && st_.clientsAlive)) return;
    }
    const bool acquiring = cur.empty() || !samePartner;

    const bool fd = cfg_.opMode == "fd";
    const auto cls = classifyStage(toks, fd);
    if (acquiring && cls.stage >= 3) return;

    if (acquiring) {
        st_.complete = false;
        resetProgressLocked(nowMs());
        st_.startedEpoch = t;
        st_.myGridLocked = cfg_.grid;
    }
    st_.lastHeardMs = nowMs();
    st_.lastPeriodS = periodFor(d.mode);
    if (!trackStageLocked(cls.stage, d.utc, fx)) return;

    const std::string t2 = toks.size() > 2 ? toks[2] : "";
    const bool grid = isGrid4(t2);
    const bool keepFull = samePartner && sender.find('/') == std::string::npos
                          && cur.find('/') != std::string::npos;
    if (!keepFull) st_.dxCall = sender;
    if (grid) st_.dxGrid = t2; else if (acquiring) st_.dxGrid.clear();
    if (cls.stage <= 2) st_.report = d.snr;
    adoptOppositeSlotLocked(d);
    setRxOffsetLocked(d.freq, fx);

    if (cls.stage == 1) {
        st_.selectedTx = 2;
    } else if (cls.stage == 2) {
        st_.rcvdReport = cls.rcvd;
        st_.selectedTx = 3;
    } else if (cls.stage == 3) {
        st_.rcvdReport = cls.rcvd;
        st_.selectedTx = 4;
        st_.complete = true; st_.completedEpoch = t;
        st_.txedThisQso = true;
        logQsoLocked(fx);
    } else if (cls.stage == 4) {
        st_.selectedTx = 5;
        st_.complete = true; st_.completedEpoch = t;
        st_.txedThisQso = true;
        logQsoLocked(fx);
    } else if (cls.stage == 5) {
        st_.complete = true; st_.completedEpoch = t;
        st_.txedThisQso = true;
        logQsoLocked(fx);

        st_.dxCall.clear(); st_.dxGrid.clear(); st_.report = 0;
        st_.selectedTx = 6;
        st_.flipped = false;
        setRxOffsetLocked(1500, fx);
        syncTxLocked(fx);
        idleAfterQsoLocked(fx);
        st_.lastEvent = "completed";
        pushLocked(fx);
        return;
    }
    syncTxLocked(fx);
    pushLocked(fx);
}

void QsoEngine::tick() {
    std::vector<std::function<void()>> fx;
    {
        std::lock_guard<std::mutex> l(mtx_);
        if (st_.dxCall.empty() || !st_.lastHeardMs) {  }
        else {

            const int cycles = st_.complete ? 1 : cfg_.retries;
            if (nowMs() - st_.lastHeardMs > cycles * 2 * st_.lastPeriodS * 1000.0)
                abortLocked(fx);
        }
    }
    for (auto& f : fx) f();
}

void QsoEngine::setDxCall(const std::string& call) {
    std::vector<std::function<void()>> fx;
    {
        std::lock_guard<std::mutex> l(mtx_);
        const std::string nu = upper(call);
        if (nu != upper(st_.dxCall)) {
            st_.complete = false;
            resetProgressLocked(nowMs());
            st_.myGridLocked = cfg_.grid;
        }
        st_.dxCall = nu;
        syncTxLocked(fx);
        pushLocked(fx);
    }
    for (auto& f : fx) f();
}
void QsoEngine::setDxGrid(const std::string& grid) {
    std::vector<std::function<void()>> fx;
    { std::lock_guard<std::mutex> l(mtx_); st_.dxGrid = upper(grid); syncTxLocked(fx); pushLocked(fx); }
    for (auto& f : fx) f();
}
void QsoEngine::setReport(int report) {
    std::vector<std::function<void()>> fx;
    { std::lock_guard<std::mutex> l(mtx_); st_.report = report; syncTxLocked(fx); pushLocked(fx); }
    for (auto& f : fx) f();
}
void QsoEngine::selectTx(int n) {
    std::vector<std::function<void()>> fx;
    {
        std::lock_guard<std::mutex> l(mtx_);
        st_.selectedTx = std::max(1, std::min(6, n));
        if (st_.selectedTx == 6) {
            st_.flipped = false;
            if (!st_.dxCall.empty()) {
                st_.complete = false;
                resetProgressLocked(nowMs());
                st_.dxCall.clear(); st_.dxGrid.clear(); st_.report = 0;
            }
        }
        syncTxLocked(fx);
        pushLocked(fx);
    }
    for (auto& f : fx) f();
}
void QsoEngine::abortQso() {
    std::vector<std::function<void()>> fx;
    { std::lock_guard<std::mutex> l(mtx_); abortLocked(fx); }
    for (auto& f : fx) f();
}
void QsoEngine::refreshTxMessage() {
    std::vector<std::function<void()>> fx;
    { std::lock_guard<std::mutex> l(mtx_); syncTxLocked(fx); }
    for (auto& f : fx) f();
}

bool QsoEngine::armRequest(std::string& err) {
    bool ok = false;
    std::string msg, mode;
    {
        std::lock_guard<std::mutex> l(mtx_);

        if (cfg_.callsign.empty() || cfg_.callsign == "N0CALL" || cfg_.callsign == "NOCALL") {
            err = "set your callsign in Settings before transmitting";
            return false;
        }
        mode = mode_;
        if (mode_ == "WSPR")
            msg = wsprMessage(cfg_.callsign, cfg_.grid, hooks_.txPowerW ? hooks_.txPowerW() : 10.0);
        else {
            auto m = messagesLocked();
            msg = m[static_cast<size_t>(st_.selectedTx - 1)];
        }
        st_.attempts = 0;
        st_.lastStage = -1;
        st_.lastHeardMs = nowMs();
        st_.cqCount = 0;
        st_.cqRefSlot = -1;
    }
    ok = hooks_.txArm ? hooks_.txArm(msg, mode, err) : false;
    std::vector<std::function<void()>> fx;
    { std::lock_guard<std::mutex> l(mtx_); pushLocked(fx); }
    for (auto& f : fx) f();
    return ok;
}

static void jsonStr(std::string& o, const char* k, const std::string& v, bool comma = true) {
    o += '"'; o += k; o += "\":\"";
    for (char c : v) {
        if (c == '"' || c == '\\') { o += '\\'; o += c; }
        else if (static_cast<unsigned char>(c) >= 0x20) o += c;
    }
    o += '"';
    if (comma) o += ',';
}

std::string QsoEngine::stateJsonLocked() const {
    return stateJsonWithEvent(st_.lastEvent);
}

std::string QsoEngine::stateJsonWithEvent(const std::string& event) const {
    auto msgs = messagesLocked();
    std::string o = "{\"type\":\"qso\",";
    o += "\"seq\":" + std::to_string(st_.seq) + ',';
    jsonStr(o, "dx_call", st_.dxCall);
    jsonStr(o, "dx_grid", st_.dxGrid);
    o += "\"report\":" + std::to_string(st_.report) + ',';
    jsonStr(o, "rcvd_report", st_.rcvdReport);
    o += "\"selected_tx\":" + std::to_string(st_.selectedTx) + ',';
    o += "\"messages\":[";
    for (size_t i = 0; i < msgs.size(); ++i) {
        std::string e; jsonStr(e, "x", msgs[i], false);
        o += e.substr(4);
        if (i + 1 < msgs.size()) o += ',';
    }
    o += "],";
    o += std::string("\"complete\":") + (st_.complete ? "true" : "false") + ',';
    o += "\"attempts\":" + std::to_string(st_.attempts) + ',';
    o += "\"retries_max\":" + std::to_string(cfg_.retries) + ',';
    o += std::string("\"parity\":{\"home\":") + (st_.homeEven ? "true" : "false") +
         ",\"effective\":" + ((st_.flipped ? st_.flipTo : st_.homeEven) ? "true" : "false") +
         ",\"flipped\":" + (st_.flipped ? "true" : "false") + "},";
    jsonStr(o, "last_event", event, false);
    o += '}';
    return o;
}

std::string QsoEngine::stateJson() const {
    std::lock_guard<std::mutex> l(mtx_);
    return stateJsonWithEvent("");
}

void QsoEngine::pushLocked(std::vector<std::function<void()>>& fx) {
    ++st_.seq;
    const std::string j = stateJsonLocked();
    st_.lastEvent.clear();
    if (hooks_.pushState) fx.push_back([h = hooks_.pushState, j] { h(j); });
}

}
