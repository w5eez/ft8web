// SPDX-License-Identifier: GPL-3.0-or-later

#include <vector>
#include <string>
#include <set>
#include <thread>
#include <atomic>
#include <mutex>
#include <unordered_set>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <sys/statvfs.h>
#include <ctime>
#include <cctype>
#include <unistd.h>
#include <sys/timex.h>

#include "crow.h"
#include "RigClient.hpp"
#include "AudioEngine.hpp"
#include "DecodeEngine.hpp"
#include "Waterfall.hpp"
#include "FreqPlan.hpp"
#include "Config.hpp"
#include "GpsClient.hpp"
#include "PotaClient.hpp"
#include "PskReporter.hpp"
#include "LogStore.hpp"
#include "WorkedLog.hpp"
#include "Uploader.hpp"
#include "QrzLookup.hpp"
#include "TxEngine.hpp"
#include "QsoEngine.hpp"
#include "HomeDir.hpp"
#include <map>

namespace {

crow::json::wvalue logJson(const ft8web::LogInfo& li) {
    crow::json::wvalue e;
    e["file"]    = li.file;
    e["name"]    = li.name;
    e["park"]    = li.park;
    e["active"]  = li.active;
    e["archived"] = li.archived;
    e["created"] = static_cast<int64_t>(li.created);
    e["qsos"]    = li.qsos;
    e["pota_uploaded"] = li.potaUploaded;
    return e;
}

ft8web::QsoFields qsoFromBody(const crow::json::rvalue& body) {
    ft8web::QsoFields q;
    for (const auto& key : body.keys()) {
        std::string k = key;
        std::transform(k.begin(), k.end(), k.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        if (k == "broadcast") continue;
        const auto& v = body[key];
        if (v.t() == crow::json::type::String) {
            std::string val = std::string(v.s());
            if (!val.empty()) q[k] = val;
        }
    }
    return q;
}

std::string jStr(const crow::json::rvalue& b, const char* k, const std::string& def = "") {
    return (b.has(k) && b[k].t() == crow::json::type::String) ? std::string(b[k].s()) : def;
}
bool jBool(const crow::json::rvalue& b, const char* k, bool def) {
    if (!b.has(k)) return def;
    const auto t = b[k].t();
    return t == crow::json::type::True ? true : (t == crow::json::type::False ? false : def);
}
double jNum(const crow::json::rvalue& b, const char* k, double def) {
    return (b.has(k) && b[k].t() == crow::json::type::Number) ? b[k].d() : def;
}

int txBandwidthFor(const std::string& mode) {
    if (mode == "FT4") return 83;
    if (mode == "FT2") return 167;
    return 50;
}

int clearTxOffset(std::vector<int> occ, int bw) {
    const int lo = 300, hi = 2700, guard = 30;
    const int tMax = hi - bw;
    if (tMax <= lo) return -1;
    const int center = (lo + hi) / 2;
    std::vector<std::pair<int, int>> blocked;
    blocked.reserve(occ.size());
    for (int f : occ) blocked.emplace_back(f - bw - guard, f + bw + guard);
    std::sort(blocked.begin(), blocked.end());
    int cur = lo, bestT = -1, bestCD = 1 << 30;
    auto consider = [&](int a, int b) {
        if (b <= a) return;
        const int t = std::max(a, std::min(b, center));
        const int d = t > center ? t - center : center - t;
        if (d < bestCD) { bestCD = d; bestT = t; }
    };
    for (const auto& [s, e] : blocked) {
        if (s > cur) consider(cur, std::min(s, tMax));
        if (e > cur) cur = e;
        if (cur >= tMax) break;
    }
    if (cur < tMax) consider(cur, tMax);
    if (bestT >= 0) return bestT;
    int bestHz = lo, bestFar = -1;
    for (int f = lo; f <= tMax; f += 10) {
        int near = 1 << 30;
        for (int o : occ) { const int d = o > f ? o - f : f - o; if (d < near) near = d; }
        if (near > bestFar) { bestFar = near; bestHz = f; }
    }
    return bestHz;
}

bool clockSynced() {
    struct timex t{};
    int s = adjtimex(&t);
    return s >= 0 && s != TIME_ERROR && !(t.status & STA_UNSYNC);
}

struct WsHub {
    std::mutex                                        m;
    std::unordered_set<crow::websocket::connection*>  conns;

    void add(crow::websocket::connection* c)    { std::lock_guard<std::mutex> l(m); conns.insert(c); }
    void remove(crow::websocket::connection* c) { std::lock_guard<std::mutex> l(m); conns.erase(c); }
    size_t size()                               { std::lock_guard<std::mutex> l(m); return conns.size(); }
    void text(const std::string& s)   { std::lock_guard<std::mutex> l(m); for (auto* c : conns) c->send_text(s); }
    void binary(const std::string& s) { std::lock_guard<std::mutex> l(m); for (auto* c : conns) c->send_binary(s); }
};

crow::json::wvalue rigJson(const ft8web::RigStatus& st) {
    crow::json::wvalue r;
    r["ok"] = st.ok;
    if (st.ok) {
        r["freq_hz"]     = static_cast<int64_t>(st.freq_hz);
        r["mode"]        = st.mode;
        r["passband"]    = st.passband;
        r["ptt"]         = st.ptt;
        r["strength_db"] = st.strength_db;
        r["power_w"]     = st.power_w;
        r["swr"]         = st.swr;
        r["alc"]         = st.alc;
    }
    return r;
}

crow::json::wvalue rigJsonWithScale(ft8web::RigClient& rig) {
    auto r = rigJson(rig.poll());
    r["power_max_w"] = rig.powerScale();
    return r;
}

crow::json::wvalue decodeJson(const ft8web::Decode& d) {
    crow::json::wvalue e;
    e["utc"] = d.utc; e["snr"] = d.snr; e["dt"] = d.dt;
    e["freq"] = d.freq; e["mode"] = d.mode; e["band"] = d.band; e["message"] = d.message;
    return e;
}

std::vector<crow::json::wvalue> modesJson() {
    std::vector<crow::json::wvalue> a;
    for (const auto& m : ft8web::DecodeEngine::modes()) {
        crow::json::wvalue e; e["name"] = m.name; e["period"] = m.period;
        a.push_back(std::move(e));
    }
    return a;
}

std::string normMsg(std::string s) {
    size_t i = s.find_first_not_of(" \t");
    size_t j = s.find_last_not_of(" \t");
    s = (i == std::string::npos) ? std::string() : s.substr(i, j - i + 1);
    for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

}

int main()
{
    crow::SimpleApp app;

    ft8web::RigClient   rig;
    ft8web::AudioEngine audio;
    audio.start("default");

    ft8web::DecodeEngine decoder(
        audio,
        ft8web::ft8webRoot("vendor/wsjtx-3.1.0/build/jt9"),
        ft8web::ft8webRoot("vendor/wsjtx-3.1.0/build/wsprd"),
        "/tmp/ft8web_work");
    decoder.setMode("FT8");

    {
        char exe[4096];
        const ssize_t n = ::readlink("/proc/self/exe", exe, sizeof exe - 1);
        if (n > 0) {
            exe[n] = '\0';
            const std::string p(exe);
            const auto slash = p.find_last_of('/');
            if (slash != std::string::npos) {
                const std::string jt9d = p.substr(0, slash) + "/jt9d";
                if (std::filesystem::exists(jt9d)) decoder.setJt9dPath(jt9d);
            }
        }
    }

    ft8web::Waterfall waterfall(audio);
    ft8web::FreqPlan  freqplan;

    WsHub hub;
    std::atomic<int> txOffset{1500};

    ft8web::Config      config(ft8web::ft8webRoot("config.json"));
    std::filesystem::create_directories(ft8web::ft8webLogs());
    ft8web::LogStore    logs(ft8web::ft8webLogs());
    ft8web::PskReporter psk;
    ft8web::Uploader    uploader(ft8web::ft8webLogs("upload_queue.json"));
    ft8web::QrzLookup   qrz;
    ft8web::GpsClient   gps;
    ft8web::WorkedLog   worked(ft8web::ft8webLogs(".qrz_worked.adi"));

    std::function<void(const ft8web::Config::Values&)> qsoApplyConfig;

    auto applyConfig = [&] {
        auto v = config.values();
        decoder.setStation(v.callsign, v.grid);
        psk.configure(v.callsign, v.grid, v.pskEnabled);
        logs.setStationCallsign(v.callsign);
        uploader.configure(v.callsign, v.grid, v.lotwUpload, v.qrzApiKey, v.qrzUpload);
        qrz.configure(v.qrzUser, v.qrzPass);
        worked.configure(v.qrzApiKey);
        if (qsoApplyConfig) qsoApplyConfig(v);
    };
    applyConfig();

    std::atomic<long long> dialFreq{0};
    std::atomic<double>    txPower{0.0};
    decoder.setDialGetter([&dialFreq] { return dialFreq.load(); });

    std::mutex selfTxMtx;
    std::vector<std::pair<std::string, std::chrono::steady_clock::time_point>> selfTxEchoes;

    std::function<void(const ft8web::Decode&)> qsoOnDecode;

    decoder.setCallback([&hub, &psk, &dialFreq, &selfTxMtx, &selfTxEchoes, &qsoOnDecode](const ft8web::Decode& d) {

        {
            double per = 15.0;
            if (const auto* ms = ft8web::DecodeEngine::findMode(d.mode)) per = ms->period;
            const std::string norm = normMsg(d.message);
            const auto now = std::chrono::steady_clock::now();
            std::lock_guard<std::mutex> lk(selfTxMtx);
            selfTxEchoes.erase(
                std::remove_if(selfTxEchoes.begin(), selfTxEchoes.end(), [&](const auto& e) {
                    return std::chrono::duration<double>(now - e.second).count() > 180.0; }),
                selfTxEchoes.end());
            for (const auto& e : selfTxEchoes)
                if (e.first == norm &&
                    std::chrono::duration<double>(now - e.second).count() <= per * 1.6)
                    return;
        }
        if (qsoOnDecode) qsoOnDecode(d);
        psk.addDecode(d.message, d.snr, d.freq, d.mode, dialFreq.load());
        crow::json::wvalue m = decodeJson(d);
        m["type"] = "decode";
        hub.text(m.dump());
    });
    decoder.start();

    std::atomic<long long> lastHeartbeatMs{0};
    auto steadyMs = [] {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    };

    std::function<void(crow::websocket::connection&)> wsSendSnapshot;

    CROW_WEBSOCKET_ROUTE(app, "/ws")
        .onopen([&](crow::websocket::connection& c) {
            hub.add(&c);
            lastHeartbeatMs.store(steadyMs());

            crow::json::wvalue modes; modes["type"] = "modes";
            modes["modes"] = modesJson();
            modes["current"] = decoder.mode();
            c.send_text(modes.dump());

            if (wsSendSnapshot) wsSendSnapshot(c);

            crow::json::wvalue back; back["type"] = "decodes";
            std::vector<crow::json::wvalue> da;
            for (const auto& d : decoder.recent(3000)) da.push_back(decodeJson(d));
            back["decodes"] = std::move(da);
            c.send_text(back.dump());
        })
        .onmessage([&](crow::websocket::connection&, const std::string&, bool) {

            lastHeartbeatMs.store(steadyMs());
        })
        .onclose([&](crow::websocket::connection& c, const std::string&, uint16_t) {
            hub.remove(&c);
        });

    std::atomic<bool>   txInhibit{false};
    std::atomic<double> swrTrip{0.0};
    std::atomic<long long> rigOkMs{0};
    const long long serverStartMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    ft8web::TxEngine txe(audio,
                         ft8web::ft8webRoot("vendor/wsjtx-3.1.0/build"),
                         "/tmp/ft8web_tx",
                         "default");

    auto disarmTx = [&] { txe.disarm(); audio.stopPlayback(); rig.setPtt(false); };

    auto notifyLogs = [&hub] { hub.text("{\"type\":\"logs_changed\"}"); };

    ft8web::QsoHooks qh;
    qh.txArm     = [&](const std::string& m, const std::string& mode, std::string& err) {
        return txe.arm(m, mode, err); };
    qh.txUpdate  = [&](const std::string& m) { txe.updateMessage(m); };
    qh.txDisarm  = disarmTx;
    qh.txArmed   = [&] { return txe.armed(); };
    qh.setRxOffset = [&](int hz) { decoder.setRxFreq(hz); };
    qh.dialHz    = [&] { return dialFreq.load(); };
    qh.txOffsetHz = [&] { return txOffset.load(); };
    qh.txPowerW  = [&] { const double p = txPower.load(); return p > 0.0 ? p : rig.powerScale(); };
    qh.radioMode = [&] { return decoder.mode(); };
    qh.logQso    = [&](const std::map<std::string, std::string>& q) {
        uploader.enqueue(logs.stampForUpload(q));
        int n = logs.addQsoToActive(q);
        logs.bumpCounter();
        uploader.activity("QSO " + q.at("call") +
                          (n > 0 ? " logged to " + std::to_string(n) + " logbook(s)"
                                 : " recorded"));
        notifyLogs();
    };
    qh.pushState = [&](const std::string& j) { hub.text(j); };
    ft8web::QsoEngine qso(std::move(qh));

    auto pickClearTx = [&] {
        if (decoder.mode() == "WSPR") return;
        const long long nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        const long long horizon = static_cast<long long>((4.0 * decoder.periodSeconds() + 4.0) * 1000.0);
        std::vector<std::string> selfMsgs;
        {
            std::lock_guard<std::mutex> lk(selfTxMtx);
            for (const auto& e : selfTxEchoes) selfMsgs.push_back(e.first);
        }
        std::vector<int> occ;
        for (const auto& d : decoder.recent(2000)) {
            if (d.freq <= 0) continue;
            if (nowMs - ft8web::QsoEngine::epochFromUtc(d.utc, nowMs) > horizon) continue;
            if (std::find(selfMsgs.begin(), selfMsgs.end(), normMsg(d.message)) != selfMsgs.end())
                continue;
            occ.push_back(d.freq);
        }
        const int bw = txBandwidthFor(decoder.mode());
        const int cur = txOffset.load();
        int nearCur = 0;
        for (int f : occ) { const int d = f > cur ? f - cur : cur - f; if (d < bw + 30) ++nearCur; }
        if (nearCur == 0 && cur >= 300 && cur <= 2700 - bw) return;
        const int tx = clearTxOffset(occ, bw);
        if (tx > 0) txOffset.store(std::max(200, std::min(2800, tx)));
    };

    qsoOnDecode = [&](const ft8web::Decode& d) { qso.onDecode(d); };
    decoder.setApContext([&qso](std::string& c, std::string& g, int& p) { qso.apContext(c, g, p); });
    qsoApplyConfig = [&](const ft8web::Config::Values& v) {
        ft8web::QsoConfigSnapshot qs;
        qs.callsign = v.callsign; qs.grid = v.grid; qs.opMode = v.opMode;
        qs.fdClass = v.fdClass; qs.fdSection = v.fdSection;
        qs.autoCq = v.autoCq; qs.homeEven = v.txEven; qs.retries = v.qsoRetries;
        qs.maxCqs = v.maxCqs;
        qs.cqSkip = v.cqSkip;
        qs.findClearFreqBeforeCq = v.findClearFreqBeforeCq;
        qso.applyConfig(qs);
    };
    qsoApplyConfig(config.values());

    auto buildStatus = [&](const ft8web::RigStatus& st) -> std::string {
        crow::json::wvalue m;
        m["type"] = "status";
        auto rj = rigJson(st);
        rj["power_max_w"] = rig.powerScale();
        m["rig"] = std::move(rj);
        {
            auto gf = gps.fix();
            crow::json::wvalue gj;
            gj["fix"]  = gf.valid;
            gj["grid"] = gf.valid ? ft8web::GpsClient::latLonToGrid(gf.lat, gf.lon)
                                  : std::string{};
            gj["lat"]  = gf.lat;
            gj["lon"]  = gf.lon;
            gj["auto"] = config.autoGrid();
            m["gps"]   = std::move(gj);
        }
        crow::json::wvalue a;
        a["running"]       = audio.running();
        a["level"]         = audio.level();
        a["device"]        = audio.device();
        a["captureVolume"] = audio.captureVolume();
        a["hasGain"]       = audio.hasCaptureVolume();
        a["clip"]          = audio.clipping();
        a["silent"]        = audio.silent();
        a["error"]         = audio.error();
        m["audio"]  = std::move(a);
        m["clock_synced"] = clockSynced();
        m["mode"]   = decoder.mode();
        m["period"] = decoder.periodSeconds();
        m["rx"]     = decoder.rxFreq();
        m["tx"]     = txOffset.load();
        m["tx_inhibit"] = txInhibit.load();
        m["swr_trip"]   = swrTrip.load();
        m["tx_armed"]   = txe.armed();
        m["tx_active"]  = txe.active();
        m["tx_message"] = txe.message();
        { const long long hb = lastHeartbeatMs.load();
          m["hb_age_ms"] = static_cast<int64_t>(hb ? steadyMs() - hb : -1); }
        m["t_ms"]   = static_cast<int64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
        return m.dump();
    };

    wsSendSnapshot = [&](crow::websocket::connection& c) {
        auto st = rig.poll(true);
        st.alc = 0.0;
        c.send_text(buildStatus(st));
        c.send_text(qso.stateJson());

        {
            auto rows = waterfall.history();
            if (!rows.empty()) {

                const uint16_t nBins = static_cast<uint16_t>(rows.back().bins.size());
                std::vector<const ft8web::Waterfall::Row*> matching;
                matching.reserve(rows.size());
                for (const auto& r : rows)
                    if (r.bins.size() == nBins) matching.push_back(&r);

                if (!matching.empty()) {
                    const uint16_t rowCount = static_cast<uint16_t>(matching.size());
                    std::string b;
                    b.reserve(5 + static_cast<size_t>(rowCount) * (8 + nBins));
                    b.push_back(static_cast<char>(0x02));
                    auto le16 = [&b](uint16_t v) {
                        b.push_back(static_cast<char>(v & 0xff));
                        b.push_back(static_cast<char>(v >> 8));
                    };
                    le16(nBins);
                    le16(rowCount);
                    for (const auto* rp : matching) {
                        uint64_t t = static_cast<uint64_t>(rp->t_ms);
                        for (int i = 0; i < 8; ++i)
                            b.push_back(static_cast<char>((t >> (8 * i)) & 0xff));
                        b.append(reinterpret_cast<const char*>(rp->bins.data()), nBins);
                    }
                    c.send_binary(b);
                }
            }
        }
    };

    {
        ft8web::TxHooks h;
        h.inhibited = [&] { return txInhibit.load(); };
        h.offsetHz  = [&] { return txOffset.load(); };
        h.slotEven  = [&] { return qso.effectiveEven(); };
        h.skipSlot  = [&](long long s) { return qso.skipTxSlot(s); };
        h.level01   = [&] {

            const float t = static_cast<float>(config.values().txLevel) / 100.0f;
            return t * t * 0.95f;
        };
        h.ptt       = [&](bool on) { rig.setPtt(on); audio.setTransmitting(on); };

        h.beforeTx  = [&](const std::string& msg) {
            if (config.values().findClearFreqBeforeCq && decoder.mode() != "WSPR" &&
                msg.rfind("CQ", 0) == 0)
                pickClearTx();
        };

        h.sent      = [&hub, &dialFreq, &selfTxMtx, &selfTxEchoes, &qso]
                      (const std::string& msg, const std::string& mode, int offsetHz) {
            {
                std::lock_guard<std::mutex> lk(selfTxMtx);
                selfTxEchoes.emplace_back(normMsg(msg), std::chrono::steady_clock::now());
                if (selfTxEchoes.size() > 16) selfTxEchoes.erase(selfTxEchoes.begin());
            }
            ft8web::Decode d;
            std::time_t tt = std::time(nullptr);
            std::tm g{}; gmtime_r(&tt, &g);
            char buf[8]; std::snprintf(buf, sizeof buf, "%02d%02d%02d", g.tm_hour, g.tm_min, g.tm_sec);
            d.utc = buf; d.snr = 0; d.dt = 0.0;
            d.freq = offsetHz; d.mode = mode;
            d.band = ft8web::FreqPlan::bandFor(dialFreq.load());
            d.message = msg;
            crow::json::wvalue m = decodeJson(d);
            m["type"] = "decode";
            m["tx"]   = true;
            hub.text(m.dump());
            qso.onDecode(d);
        };
        h.txDone    = [&qso] { qso.onTxDone(); };
        txe.setHooks(std::move(h));
    }

    auto applyRadio = [&] {
        const auto v = config.values();
        if (v.radios.empty()) return;
        const int idx = (v.activeRadio >= 0 && v.activeRadio < static_cast<int>(v.radios.size()))
                        ? v.activeRadio : 0;
        const auto& rd = v.radios[idx];
        rig.setRadio(rd.model, rd.serial, rd.baud, rd.powerMaxW);
        audio.setChannel(rd.rxChannel);
        if (!rd.device.empty() && rd.device != audio.device()) audio.start(rd.device);
        if (!rd.device.empty()) txe.setPlayDevice(ft8web::AudioEngine::resolveDevice(rd.device));
        std::fprintf(stderr, "[radio] active: %s (hamlib model %d, %s @ %d, audio=%s)\n",
                     rd.name.c_str(), rd.model, rd.serial.c_str(), rd.baud, rd.device.c_str());
    };
    applyRadio();

    auto applyDataMode = [&] {
        const auto v = config.values();
        if (v.activeRadio < 0 || v.activeRadio >= static_cast<int>(v.radios.size())) return;
        const auto& rd = v.radios[v.activeRadio];
        if (!rd.dataMode.empty()) rig.setMode(rd.dataMode, rd.rxFilterHz > 0 ? rd.rxFilterHz : -1);
    };

    auto switchMode = [&](const std::string& m) {
        const std::string old = decoder.mode();
        decoder.setMode(m);
        if (decoder.mode() != old) {
            config.update([&](ft8web::Config::Values& v) { v.radioMode = decoder.mode(); });
            if (txe.armed()) disarmTx();
        }
        qso.onModeChange(decoder.mode());
        applyDataMode();
    };

    {
        const auto v = config.values();
        switchMode(v.radioMode.empty() ? "FT8" : v.radioMode);
        if (v.radioDialHz > 0) {
            rig.setFrequency(v.radioDialHz);
            dialFreq.store(v.radioDialHz);
        }
        std::fprintf(stderr, "[boot] restored mode=%s dial=%lld Hz\n",
                     decoder.mode().c_str(), static_cast<long long>(dialFreq.load()));
    }

    auto killTx = [&](double swr) {
        txInhibit.store(true);
        swrTrip.store(swr);
        txe.disarm();
        rig.setPtt(false);
        audio.stopPlayback();
        std::fprintf(stderr, "[swr] TX inhibited: SWR %.2f exceeded limit\n", swr);
    };

    std::atomic<bool> pumpRun{true};
    std::thread pump([&] {
        int tick = 0;
        int swrFail = 0;
        double alcPeak = 0.0;
        double lastSwr = 0.0;
        long long savedDial   = config.values().radioDialHz;
        long long pendingDial = 0;
        int       pendingHits = 0;
        while (pumpRun.load()) {
            bool haveSwr = false;

            if (tick % 600 == 0 && !clockSynced())
                std::fprintf(stderr, "[clock] system time is NOT NTP-synced — FT8 decodes "
                                     "will fail until the clock is set (GPS/NTP)\n");

            if (txe.active() && !txInhibit.load()) {

                auto sv = config.values();
                if (sv.swrProtect) {
                    if (auto s = rig.getLevel("SWR")) {
                        swrFail = 0;
                        lastSwr = *s; haveSwr = true;
                        if (*s > sv.swrMax) killTx(*s);
                    } else if (++swrFail >= 10) {
                        std::fprintf(stderr, "[swr] SWR unreadable while keyed — inhibiting TX\n");
                        killTx(-1.0);
                    }
                }
            } else {
                swrFail = 0;
            }

            if (txe.active()) {
                if (tick % 2 == 0) {
                    alcPeak *= 0.85;
                    if (auto a = rig.getLevel("ALC")) alcPeak = std::max(*a, alcPeak);
                }
            } else {
                alcPeak = 0.0;
            }

            if (tick % 10 == 0 && config.autoGrid() && !qso.qsoActive()) {
                std::string g = gps.grid();
                for (auto& c : g) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                if (!g.empty() && config.grid() != g) {
                    config.update([&](auto& v) { v.grid = g; });
                    applyConfig();
                    std::fprintf(stderr, "[gps] AutoGrid -> %s\n", g.c_str());
                }
            }

            if (tick % 30 == 0 && !audio.running()) {
                const auto v = config.values();
                if (!v.radios.empty()) {
                    const int idx = (v.activeRadio >= 0 &&
                                     v.activeRadio < static_cast<int>(v.radios.size()))
                                    ? v.activeRadio : 0;
                    const std::string dev = v.radios[idx].device;
                    const std::string pcm = dev.empty() ? std::string()
                                          : ft8web::AudioEngine::resolveDevice(dev);
                    if (!pcm.empty() && audio.start(dev)) {
                        txe.setPlayDevice(pcm);
                        std::fprintf(stderr, "[audio] recovered capture device: %s\n", dev.c_str());
                    }
                }
            }

            {
                const long long hb = lastHeartbeatMs.load();
                const bool noClient = hub.size() == 0 || hb == 0 || steadyMs() - hb > 10000;
                qso.clientsAlive(!noClient);
                if (noClient && txe.armed() && !qso.qsoInProgress()) {
                    txe.disarm();
                    audio.stopPlayback();
                    rig.setPtt(false);
                    std::fprintf(stderr, "[client] no client heartbeat >10s — TX disabled\n");
                }
            }

            if (tick % 10 == 0) qso.tick();

            ft8web::RigStatus st;
            if (tick % 10 == 0) {
                waterfall.setWsprView(decoder.mode() == "WSPR");
                st = rig.poll(!haveSwr);
                if (st.ok) {
                    dialFreq.store(st.freq_hz);
                    if (st.power_w > 0.0) txPower.store(st.power_w);
                    rigOkMs.store(std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count());
                    if (st.freq_hz > 0 && st.freq_hz != savedDial) {
                        if (st.freq_hz == pendingDial) {
                            if (++pendingHits >= 2) {
                                config.update([&](ft8web::Config::Values& v) { v.radioDialHz = pendingDial; });
                                savedDial   = pendingDial;
                                pendingHits = 0;
                            }
                        } else {
                            pendingDial = st.freq_hz;
                            pendingHits = 1;
                        }
                    }
                }
                if (haveSwr) st.swr = lastSwr;
                st.alc = alcPeak;
            }

            if (hub.size() > 0 && tick % 10 == 0) hub.text(buildStatus(st));
            ++tick;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    });

    std::thread wfThread([&] {
        while (pumpRun.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            if (hub.size() == 0) continue;
            auto f = waterfall.frame();
            std::string bin;
            bin.reserve(f.size() + 1);
            bin.push_back(static_cast<char>(0x01));
            bin.append(reinterpret_cast<const char*>(f.data()), f.size());
            hub.binary(bin);
        }
    });

    const std::string kWebRoot =
        ft8web::ft8webRoot("frontend/ft8web-ui/dist/ft8web-ui/browser");

    CROW_ROUTE(app, "/")
    ([kWebRoot](const crow::request&, crow::response& res) {
        const std::string index = kWebRoot + "/index.html";
        if (std::filesystem::exists(index)) {
            res.set_static_file_info_unsafe(index);
            res.set_header("Cache-Control", "no-cache");
        } else {
            res.set_header("Content-Type", "text/plain");
            res.body = "ft8web backend is running (frontend not built yet)";
        }
        res.end();
    });

    CROW_ROUTE(app, "/api/health")
    ([] {
        crow::json::wvalue r;
        r["status"] = "ok"; r["service"] = "ft8web"; r["version"] = "0.1.0";
        return r;
    });

    CROW_ROUTE(app, "/api/rig")
    ([&rig] {
        return rigJsonWithScale(rig);
    });

    CROW_ROUTE(app, "/api/audio").methods("GET"_method, "POST"_method)
    ([&audio, &config, &txe](const crow::request& req) {
        if (req.method == "POST"_method) {
            auto body = crow::json::load(req.body);
            if (body) {

                { auto d = jStr(body, "device"); if (!d.empty()) {
                    config.update([&](ft8web::Config::Values& v) {
                        if (v.activeRadio >= 0 && v.activeRadio < static_cast<int>(v.radios.size()))
                            v.radios[v.activeRadio].device = d;
                    });
                    audio.start(d);
                    txe.setPlayDevice(ft8web::AudioEngine::resolveDevice(d));
                } }
                { int g = static_cast<int>(jNum(body, "gain", -1)); if (g >= 0) audio.setCaptureVolume(g); }
            }
        }
        crow::json::wvalue r;
        r["running"]       = audio.running();
        r["device"]        = audio.device();
        r["level"]         = audio.level();
        r["captureVolume"] = audio.captureVolume();
        r["hasGain"]       = audio.hasCaptureVolume();
        r["clip"]          = audio.clipping();
        r["silent"]        = audio.silent();
        r["error"]         = audio.error();
        std::vector<crow::json::wvalue> arr;
        for (const auto& d : ft8web::AudioEngine::listCaptureDevices()) {
            crow::json::wvalue e; e["id"] = d.id; e["name"] = d.name;
            arr.push_back(std::move(e));
        }
        r["devices"] = std::move(arr);
        return r;
    });

    CROW_ROUTE(app, "/api/decodes")
    ([&decoder] {
        crow::json::wvalue r;
        std::vector<crow::json::wvalue> arr;
        for (const auto& d : decoder.recent(3000)) arr.push_back(decodeJson(d));
        r["decodes"] = std::move(arr);
        return r;
    });

    CROW_ROUTE(app, "/api/mode").methods("GET"_method, "POST"_method)
    ([&decoder, &switchMode](const crow::request& req) {
        if (req.method == "POST"_method) {
            auto body = crow::json::load(req.body);
            if (body && body.has("mode")) switchMode(std::string(body["mode"].s()));
        }
        crow::json::wvalue r;
        r["mode"] = decoder.mode();
        r["available"] = modesJson();
        return r;
    });

    CROW_ROUTE(app, "/api/spectrum")
    ([&waterfall] {
        auto f = waterfall.frame();
        crow::json::wvalue r;
        r["bins"]     = waterfall.bins();
        r["bin_hz"]   = waterfall.binHz();
        r["start_hz"] = waterfall.startHz();
        std::vector<crow::json::wvalue> arr;
        arr.reserve(f.size());
        for (auto b : f) arr.push_back(static_cast<int>(b));
        r["data"] = std::move(arr);
        return r;
    });

    CROW_ROUTE(app, "/api/frequencies")
    ([&freqplan](const crow::request& req) {
        const char* mp = req.url_params.get("mode");
        const char* rp = req.url_params.get("region");
        std::string region = rp ? rp : "ALL";
        auto list = (mp && *mp) ? freqplan.forMode(mp, region) : freqplan.all();
        crow::json::wvalue r;
        std::vector<crow::json::wvalue> arr;
        for (const auto& e : list) {
            crow::json::wvalue x;
            x["hz"] = static_cast<int64_t>(e.hz); x["mode"] = e.mode;
            x["band"] = e.band; x["region"] = e.region;
            arr.push_back(std::move(x));
        }
        r["frequencies"] = std::move(arr);
        std::vector<crow::json::wvalue> ms;
        for (const auto& m : freqplan.modes()) ms.push_back(m);
        r["modes"] = std::move(ms);
        return r;
    });

    CROW_ROUTE(app, "/api/tune").methods("POST"_method)
    ([&freqplan, &rig, &config, &switchMode](const crow::request& req) {
        crow::json::wvalue r;
        auto body = crow::json::load(req.body);
        if (!body || !body.has("mode") || !body.has("band")) {
            r["ok"] = false; r["error"] = "mode and band required";
            return r;
        }
        std::string mode   = std::string(body["mode"].s());
        std::string band   = std::string(body["band"].s());

        std::string region = freqplan.regionForGrid(config.grid());
        if (body.has("region")) {
            std::string rr = std::string(body["region"].s());
            if (rr == "R1" || rr == "R2" || rr == "R3") region = rr;
        }
        switchMode(mode);

        auto hz = freqplan.frequencyFor(mode, band, region);
        r["mode"] = mode; r["band"] = band;
        if (hz && rig.setFrequency(*hz)) {
            r["ok"] = true; r["freq_hz"] = static_cast<int64_t>(*hz);
        } else {
            r["ok"] = false;
            r["error"] = hz ? "rig set_freq failed" : "no default frequency for mode/band";
        }
        return r;
    });

    CROW_ROUTE(app, "/api/tx").methods("POST"_method)
    ([&txe, &qso, &disarmTx](const crow::request& req) {
        crow::json::wvalue r;
        auto body = crow::json::load(req.body);

        if (!body || !body.has("on") ||
            (body["on"].t() != crow::json::type::True &&
             body["on"].t() != crow::json::type::False)) {
            r["ok"] = false;
            r["error"] = "on (bool) required";
            r["armed"] = txe.armed();
            return r;
        }
        const bool on = body["on"].b();

        if (!on) {
            disarmTx();
            std::fprintf(stderr, "[tx] api DISARM\n");
            r["ok"] = true;
            r["armed"] = false;
            return r;
        }
        std::string err;
        const bool ok = qso.armRequest(err);
        std::fprintf(stderr, "[tx] api ARM -> ok=%d\n", ok ? 1 : 0);
        r["ok"] = ok;
        r["armed"] = txe.armed();
        if (!ok) r["error"] = err;
        return r;
    });

    CROW_ROUTE(app, "/api/rig/tx-clear").methods("POST"_method)
    ([&txInhibit, &swrTrip] {
        txInhibit.store(false);
        swrTrip.store(0.0);
        crow::json::wvalue r;
        r["ok"] = true;
        return r;
    });

    CROW_ROUTE(app, "/api/restart").methods("POST"_method)
    ([&txe, &rig, &audio] {
        txe.disarm();
        audio.stopPlayback();
        rig.setPtt(false);
        std::thread([] {
            std::this_thread::sleep_for(std::chrono::milliseconds(400));
            ::_exit(0);
        }).detach();
        crow::json::wvalue r;
        r["ok"] = true;
        return r;
    });

    CROW_ROUTE(app, "/api/shutdown").methods("POST"_method)
    ([&txe, &rig, &audio] {
        txe.disarm();
        audio.stopPlayback();
        rig.setPtt(false);
        std::thread([] {
            std::this_thread::sleep_for(std::chrono::milliseconds(400));
            std::system("sudo systemctl poweroff");
        }).detach();
        crow::json::wvalue r;
        r["ok"] = true;
        return r;
    });

    CROW_ROUTE(app, "/api/rig/power").methods("POST"_method)
    ([&rig, &qso, &txPower](const crow::request& req) {
        crow::json::wvalue r;
        auto body = crow::json::load(req.body);
        if (!body || !body.has("watts") || body["watts"].t() != crow::json::type::Number) { r["ok"] = false; r["error"] = "watts (number) required"; return r; }
        const double scale = rig.powerScale();
        double w = body["watts"].d();
        w = std::max(0.5, std::min(scale, w));
        const bool ok = rig.setLevel("RFPOWER", w / scale);
        if (ok) {
            txPower.store(w);
            qso.refreshTxMessage();
        }
        r["ok"]      = ok;
        r["watts"]   = w;
        return r;
    });

    CROW_ROUTE(app, "/api/config").methods("GET"_method, "POST"_method)
    ([&config, &applyConfig](const crow::request& req) {
        if (req.method == "POST"_method) {
            auto body = crow::json::load(req.body);
            if (body) {
                config.update([&](ft8web::Config::Values& v) {
                    v.callsign   = jStr(body, "callsign",   v.callsign);
                    v.grid       = jStr(body, "grid",       v.grid);
                    v.pskEnabled = jBool(body, "psk_enabled", v.pskEnabled);
                    v.opMode     = jStr(body, "op_mode",    v.opMode);
                    v.fdClass    = jStr(body, "fd_class",   v.fdClass);
                    v.fdSection  = jStr(body, "fd_section", v.fdSection);
                    v.lotwUser   = jStr(body, "lotw_user",  v.lotwUser);

                    { auto s = jStr(body, "lotw_pass");   if (!s.empty()) v.lotwPass  = s; }
                    { auto s = jStr(body, "qrz_api_key"); if (!s.empty()) v.qrzApiKey = s; }
                    v.qrzUser    = jStr(body, "qrz_user",  v.qrzUser);
                    { auto s = jStr(body, "qrz_pass");    if (!s.empty()) v.qrzPass   = s; }
                    v.potaUser   = jStr(body, "pota_user", v.potaUser);
                    { auto s = jStr(body, "pota_pass");   if (!s.empty()) v.potaPass  = s; }
                    v.lotwUpload = jBool(body, "lotw_upload", v.lotwUpload);
                    v.qrzUpload  = jBool(body, "qrz_upload",  v.qrzUpload);
                    v.txEven      = jBool(body, "tx_even",       v.txEven);
                    v.autoCq      = jBool(body, "auto_cq",       v.autoCq);
                    v.qsoRetries  = static_cast<int>(jNum(body, "qso_retries", v.qsoRetries));
                    v.maxCqs      = static_cast<int>(jNum(body, "max_cqs", v.maxCqs));
                    v.cqSkip      = jBool(body, "cq_skip",       v.cqSkip);
                    v.findClearFreqBeforeCq = jBool(body, "find_clear_freq_before_cq", v.findClearFreqBeforeCq);
                    v.txLevel     = static_cast<int>(jNum(body, "tx_level",    v.txLevel));
                    v.swrProtect = jBool(body, "swr_protect", v.swrProtect);
                    v.swrMax     = jNum(body, "swr_max",  v.swrMax);
                    v.autoGrid   = jBool(body, "auto_grid",   v.autoGrid);
                    v.units      = jStr(body, "units", v.units);
                    v.wfGain     = jNum(body, "wf_gain", v.wfGain);
                    v.wfPalette  = jStr(body, "wf_palette", v.wfPalette);
                    v.showCalls  = jBool(body, "show_calls", v.showCalls);
                });
                applyConfig();
            }
        }
        crow::json::wvalue r;
        auto v = config.values();
        r["callsign"]    = v.callsign;
        r["grid"]        = v.grid;
        r["psk_enabled"] = v.pskEnabled;
        r["op_mode"]     = v.opMode;
        r["fd_class"]    = v.fdClass;
        r["fd_section"]  = v.fdSection;
        r["lotw_user"]       = v.lotwUser;
        r["lotw_pass"]       = "";
        r["lotw_pass_set"]   = !v.lotwPass.empty();
        r["qrz_api_key"]     = "";
        r["qrz_api_key_set"] = !v.qrzApiKey.empty();
        r["qrz_user"]        = v.qrzUser;
        r["qrz_pass"]        = "";
        r["qrz_pass_set"]    = !v.qrzPass.empty();
        r["pota_user"]       = v.potaUser;
        r["pota_pass"]       = "";
        r["pota_pass_set"]   = !v.potaPass.empty();
        r["lotw_upload"] = v.lotwUpload;
        r["qrz_upload"]  = v.qrzUpload;
        r["tx_even"]      = v.txEven;
        r["auto_cq"]      = v.autoCq;
        r["qso_retries"]  = v.qsoRetries;
        r["max_cqs"]      = v.maxCqs;
        r["cq_skip"]      = v.cqSkip;
        r["find_clear_freq_before_cq"] = v.findClearFreqBeforeCq;
        r["tx_level"]     = v.txLevel;
        r["swr_protect"] = v.swrProtect;
        r["swr_max"]     = v.swrMax;
        r["auto_grid"]   = v.autoGrid;
        r["units"]       = v.units;
        r["wf_gain"]     = v.wfGain;
        r["wf_palette"]  = v.wfPalette;
        r["show_calls"]  = v.showCalls;
        return r;
    });

    CROW_ROUTE(app, "/api/radios").methods("GET"_method, "POST"_method)
    ([&config, &applyRadio](const crow::request& req) {
        if (req.method == "POST"_method) {
            auto body = crow::json::load(req.body);
            if (body) {
                config.update([&](ft8web::Config::Values& v) {
                    if (body.has("radios") && body["radios"].t() == crow::json::type::List) {
                        v.radios.clear();
                        for (const auto& rj : body["radios"]) {
                            ft8web::Config::Radio rd;
                            rd.name      = jStr(rj, "name", "Radio");
                            rd.model     = static_cast<int>(jNum(rj, "model", 0));
                            rd.serial    = jStr(rj, "serial");
                            rd.baud      = static_cast<int>(jNum(rj, "baud", 38400));
                            rd.device    = jStr(rj, "device");
                            rd.powerMaxW = jNum(rj, "power_max_w", 100.0);
                            rd.dataMode  = jStr(rj, "data_mode", "PKTUSB");
                            rd.rxChannel = static_cast<int>(jNum(rj, "rx_channel", 0));
                            rd.rxFilterHz = static_cast<int>(jNum(rj, "rx_filter_hz", 3000));
                            v.radios.push_back(std::move(rd));
                        }
                    }
                    if (body.has("active")) v.activeRadio = static_cast<int>(jNum(body, "active", 0));
                    if (v.radios.empty())
                        v.radios.push_back({"FT-991A", 1035, "", 38400, "", 100.0});
                    if (v.activeRadio < 0 || v.activeRadio >= static_cast<int>(v.radios.size())) v.activeRadio = 0;
                });
                applyRadio();
            }
        }
        crow::json::wvalue r;
        const auto v = config.values();
        std::vector<crow::json::wvalue> arr;
        for (const auto& rd : v.radios) {
            crow::json::wvalue e;
            e["name"]        = rd.name;
            e["model"]       = rd.model;
            e["serial"]      = rd.serial;
            e["baud"]        = rd.baud;
            e["device"]      = rd.device;
            e["power_max_w"] = rd.powerMaxW;
            e["data_mode"]   = rd.dataMode;
            e["rx_channel"]  = rd.rxChannel;
            e["rx_filter_hz"] = rd.rxFilterHz;
            arr.push_back(std::move(e));
        }
        r["radios"] = std::move(arr);
        r["active"] = v.activeRadio;
        return r;
    });

    CROW_ROUTE(app, "/api/hamlib/rigs")([] {
        crow::json::wvalue r;
        std::vector<crow::json::wvalue> arr;
        for (const auto& m : ft8web::RigClient::listModels()) {
            crow::json::wvalue e;
            e["model"]  = m.model;
            e["mfg"]    = m.mfg;
            e["name"]   = m.name;
            e["status"] = m.status;
            arr.push_back(std::move(e));
        }
        r["rigs"] = std::move(arr);
        return r;
    });

    CROW_ROUTE(app, "/api/serial/ports")([] {
        crow::json::wvalue r;
        std::vector<crow::json::wvalue> arr;
        std::error_code ec;
        for (const auto& de : std::filesystem::directory_iterator("/dev/serial/by-id", ec)) {
            crow::json::wvalue e;
            e["path"] = de.path().string();
            e["name"] = de.path().filename().string();
            arr.push_back(std::move(e));
        }
        r["ports"] = std::move(arr);
        return r;
    });

    CROW_ROUTE(app, "/api/pskreporter/reports")
    ([&psk, &config] {
        crow::json::wvalue r;
        const bool active = config.pskEnabled() && !config.callsign().empty();
        const long long now = static_cast<long long>(std::time(nullptr));
        auto nextDue = [&](long long last) -> int64_t {
            if (!active) return 0;
            return last == 0 ? now : last + 300;
        };
        r["enabled"]  = active;
        r["callsign"] = config.callsign();
        r["last_query"]  = static_cast<int64_t>(psk.lastQuerySec());
        r["now"]         = static_cast<int64_t>(now);
        r["next_query"]  = nextDue(psk.lastQuerySec());
        r["next_upload"] = nextDue(psk.lastUploadSec());
        std::vector<crow::json::wvalue> arr;
        for (const auto& x : psk.reports()) {
            crow::json::wvalue e;
            e["receiver"] = x.receiver;
            e["locator"]  = x.locator;
            e["freq"]     = static_cast<int64_t>(x.freq);
            e["mode"]     = x.mode;
            e["snr"]      = x.snr;
            e["t"]        = static_cast<int64_t>(x.t);
            arr.push_back(std::move(e));
        }
        r["reports"] = std::move(arr);
        return r;
    });

    CROW_ROUTE(app, "/api/logs").methods("GET"_method, "POST"_method)
    ([&logs, &notifyLogs](const crow::request& req) {

        crow::json::wvalue r;
        if (req.method == "POST"_method) {
            auto body = crow::json::load(req.body);
            if (!body) { r["ok"] = false; r["error"] = "bad json"; return r; }
            std::string name = jStr(body, "name");
            std::string park = jStr(body, "park");
            bool active      = jBool(body, "active", true);
            auto li = logs.create(name, park, active);
            if (!li) { r["ok"] = false; r["error"] = "create failed"; return r; }
            notifyLogs();
            r["ok"] = true;
            r["log"] = logJson(*li);
        }
        std::vector<crow::json::wvalue> arr;
        for (const auto& li : logs.list()) arr.push_back(logJson(li));
        r["logs"] = std::move(arr);
        r["counter"] = logs.qsoCounter();
        return r;
    });

    CROW_ROUTE(app, "/api/logs/counter/reset").methods("POST"_method)
    ([&logs, &notifyLogs] {
        logs.resetCounter();
        notifyLogs();
        crow::json::wvalue r;
        r["ok"] = true;
        r["counter"] = 0;
        return r;
    });

    CROW_ROUTE(app, "/api/logs/<string>").methods("DELETE"_method)
    ([&logs, &notifyLogs](const std::string& file) {
        crow::json::wvalue r;
        r["ok"] = logs.remove(file);
        notifyLogs();
        return r;
    });

    CROW_ROUTE(app, "/api/logs/<string>/meta").methods("POST"_method)
    ([&logs, &notifyLogs](const crow::request& req, const std::string& file) {
        crow::json::wvalue r;
        auto body = crow::json::load(req.body);
        if (!body) { r["ok"] = false; return r; }
        std::string name, park;
        bool active = false, archived = false;
        const std::string* pn = nullptr;
        const std::string* pp = nullptr;
        const bool* pa = nullptr;
        const bool* par = nullptr;
        if (body.has("name"))     { name = jStr(body, "name"); pn = &name; }
        if (body.has("park"))     { park = jStr(body, "park"); pp = &park; }
        if (body.has("active"))   { active = jBool(body, "active", true); pa = &active; }
        if (body.has("archived")) { archived = jBool(body, "archived", false); par = &archived; }
        r["ok"] = logs.setMeta(file, pn, pp, pa, par);
        notifyLogs();
        return r;
    });

    CROW_ROUTE(app, "/api/logs/<string>/qsos").methods("GET"_method, "POST"_method)
    ([&logs, &uploader, &notifyLogs](const crow::request& req, const std::string& file) {
        crow::json::wvalue r;
        if (req.method == "POST"_method) {
            auto body = crow::json::load(req.body);
            if (!body) { r["ok"] = false; return r; }
            auto q = qsoFromBody(body);
            const bool ok = logs.addQso(file, q);
            if (ok) {
                uploader.enqueue(logs.stampForFile(file, q));
                logs.bumpCounter();
                notifyLogs();
            }
            r["ok"] = ok;
        }
        std::vector<crow::json::wvalue> arr;
        for (const auto& q : logs.qsos(file)) {
            crow::json::wvalue e;
            for (const auto& kv : q) e[kv.first] = kv.second;
            arr.push_back(std::move(e));
        }
        r["qsos"] = std::move(arr);
        return r;
    });

    CROW_ROUTE(app, "/api/logs/<string>/qsos/<uint>").methods("POST"_method, "DELETE"_method)
    ([&logs, &notifyLogs](const crow::request& req, const std::string& file, unsigned idx) {
        crow::json::wvalue r;
        if (req.method == "DELETE"_method) {
            r["ok"] = logs.deleteQso(file, idx);
        } else {
            auto body = crow::json::load(req.body);
            if (!body) { r["ok"] = false; return r; }
            r["ok"] = logs.updateQso(file, idx, qsoFromBody(body));
        }
        notifyLogs();
        return r;
    });

    CROW_ROUTE(app, "/api/qso").methods("GET"_method)
    ([&qso] { return crow::response(200, "application/json", qso.stateJson()); });

    CROW_ROUTE(app, "/api/qso/call").methods("POST"_method)
    ([&qso](const crow::request& req) {
        crow::json::wvalue r;
        auto body = crow::json::load(req.body);
        if (!body) { r["ok"] = false; return r; }
        if (body.has("message")) {
            ft8web::Decode d;
            d.utc  = jStr(body, "utc");
            d.snr  = static_cast<int>(jNum(body, "snr",  0));
            d.freq = static_cast<int>(jNum(body, "freq", 0));
            d.mode = jStr(body, "mode");
            d.message = jStr(body, "message");
            qso.callDecode(d);
        } else {
            if (body.has("dx_call")) qso.setDxCall(jStr(body, "dx_call"));
            if (body.has("dx_grid")) qso.setDxGrid(jStr(body, "dx_grid"));
            if (body.has("report"))  qso.setReport(static_cast<int>(jNum(body, "report", 0)));
        }
        r["ok"] = true;
        return r;
    });

    CROW_ROUTE(app, "/api/qso/tx").methods("POST"_method)
    ([&qso](const crow::request& req) {
        crow::json::wvalue r;
        auto body = crow::json::load(req.body);
        if (!body || !body.has("n")) { r["ok"] = false; r["error"] = "n required"; return r; }
        qso.selectTx(static_cast<int>(jNum(body, "n", 0)));
        r["ok"] = true;
        return r;
    });

    CROW_ROUTE(app, "/api/qso/abort").methods("POST"_method)
    ([&qso] { qso.abortQso(); crow::json::wvalue r; r["ok"] = true; return r; });

    CROW_ROUTE(app, "/api/worked").methods("GET"_method)
    ([&worked] {
        auto s = worked.snapshot();
        auto toArr = [](const std::vector<std::string>& v) {
            std::vector<crow::json::wvalue> a; a.reserve(v.size());
            for (const auto& x : v) a.push_back(x);
            return a;
        };
        crow::json::wvalue r;
        r["qsos"]     = s.qsos;
        r["synced"]   = static_cast<int64_t>(s.syncedSec);
        r["syncing"]  = s.syncing;
        r["have_key"] = s.haveKey;
        r["error"]    = s.error;
        r["calls"]    = toArr(s.calls);
        r["slots"]    = toArr(s.slots);
        r["grids"]    = toArr(s.grids);
        return r;
    });

    CROW_ROUTE(app, "/api/worked/sync").methods("POST"_method)
    ([&worked] { worked.requestSync(); crow::json::wvalue r; r["ok"] = true; return r; });

    static std::mutex parksDbMtx;
    static std::vector<ft8web::PotaClient::Park> parksDb;
    const std::string parksCsvPath = ft8web::ft8webLogs("all_parks.csv");
    {
        auto db = ft8web::PotaClient::loadParksCsv(parksCsvPath);
        if (!db.empty()) {
            std::lock_guard<std::mutex> l(parksDbMtx);
            parksDb = std::move(db);
            std::fprintf(stderr, "[pota] offline park db: %zu parks\n", parksDb.size());
        } else {
            std::thread([parksCsvPath] {
                std::string err;
                if (ft8web::PotaClient::downloadParksCsv(parksCsvPath, err)) {
                    auto fresh = ft8web::PotaClient::loadParksCsv(parksCsvPath);
                    std::lock_guard<std::mutex> l(parksDbMtx);
                    parksDb = std::move(fresh);
                    std::fprintf(stderr, "[pota] park db downloaded: %zu parks\n", parksDb.size());
                } else {
                    std::fprintf(stderr, "[pota] park db download failed: %s\n", err.c_str());
                }
            }).detach();
        }
    }

    CROW_ROUTE(app, "/api/pota/refresh-parks").methods("POST"_method)
    ([parksCsvPath] {
        crow::json::wvalue r;
        std::string err;
        if (!ft8web::PotaClient::downloadParksCsv(parksCsvPath, err)) {
            r["ok"] = false;
            r["error"] = err;
            return r;
        }
        auto fresh = ft8web::PotaClient::loadParksCsv(parksCsvPath);
        size_t n;
        {
            std::lock_guard<std::mutex> l(parksDbMtx);
            parksDb = std::move(fresh);
            n = parksDb.size();
        }
        r["ok"] = true;
        r["parks"] = static_cast<int64_t>(n);
        return r;
    });

    CROW_ROUTE(app, "/api/pota/parks")
    ([&gps, &config](const crow::request& req) {
        crow::json::wvalue r;

        double clat = 0.0, clon = 0.0;
        std::string src = "gps";
        const char *latp = req.url_params.get("lat"), *lonp = req.url_params.get("lon");
        auto f = gps.fix();
        if (latp && lonp) {
            clat = std::atof(latp); clon = std::atof(lonp);
            src = "browser";
        } else if (f.valid) {
            clat = f.lat; clon = f.lon;
        } else if (ft8web::GpsClient::gridToLatLon(config.values().grid, clat, clon)) {
            src = "grid";
        } else {
            r["ok"] = false;
            r["error"] = "no GPS fix and no grid set";
            return r;
        }

        double s, w, n, e;
        const char *sp = req.url_params.get("s"), *wp = req.url_params.get("w"),
                   *np = req.url_params.get("n"), *ep = req.url_params.get("e");
        if (sp && wp && np && ep) { s = std::atof(sp); w = std::atof(wp); n = std::atof(np); e = std::atof(ep); }
        else { s = clat - 0.7; w = clon - 0.7; n = clat + 0.7; e = clon + 0.7; }
        std::vector<ft8web::PotaClient::Park> parks;
        bool offline = false;
        {
            std::lock_guard<std::mutex> l(parksDbMtx);
            if (!parksDb.empty()) {
                parks = ft8web::PotaClient::parksInBoxLocal(parksDb, s, w, n, e, clat, clon, 150);
                offline = true;
            }
        }
        if (!offline) parks = ft8web::PotaClient::parksInBox(s, w, n, e, clat, clon, 150);
        std::vector<crow::json::wvalue> arr;
        for (const auto& p : parks) {
            crow::json::wvalue ej;
            ej["reference"] = p.reference; ej["name"] = p.name;
            ej["lat"] = p.lat; ej["lon"] = p.lon; ej["dist_km"] = p.distKm;
            arr.push_back(std::move(ej));
        }
        r["ok"] = true;
        r["lat"] = clat; r["lon"] = clon;
        r["src"] = src;
        r["parks"] = std::move(arr);
        return r;
    });

    CROW_ROUTE(app, "/api/pota/park/<string>")
    ([](const std::string& ref) {
        crow::json::wvalue r;
        auto d = ft8web::PotaClient::parkDetail(ref);
        r["ok"] = d.ok;
        if (d.ok) {
            r["reference"]     = d.reference;
            r["name"]          = d.name;
            r["grid"]          = d.grid;
            r["location"]      = d.locationName;
            r["location_desc"] = d.locationDesc;
            r["parktype"]      = d.parktype;
            r["website"]       = d.website;
            r["lat"]           = d.lat;
            r["lon"]           = d.lon;
            r["activations"]   = d.activations;
            r["attempts"]      = d.attempts;
            r["qsos"]          = d.qsos;
        }
        return r;
    });

    static std::mutex              potaActMtx;
    static std::vector<std::string> potaActRefs;
    static long long               potaActFetched = 0, potaActAttempt = 0;
    CROW_ROUTE(app, "/api/pota/activated")
    ([&config] {
        crow::json::wvalue r;
        auto cv = config.values();
        if (cv.potaUser.empty() || cv.potaPass.empty()) {
            r["ok"] = false;
            r["error"] = "POTA account not set";
            return r;
        }
        const long long now = std::time(nullptr);
        std::lock_guard<std::mutex> l(potaActMtx);
        if ((potaActFetched == 0 || now - potaActFetched > 3600) && now - potaActAttempt > 120) {
            potaActAttempt = now;
            auto res = ft8web::PotaClient::userActivations(cv.potaUser, cv.potaPass);
            if (res.ok) { potaActRefs = res.refs; potaActFetched = now; }
        }
        r["ok"] = potaActFetched != 0;
        std::vector<crow::json::wvalue> arr;
        for (const auto& s : potaActRefs) arr.push_back(s);
        r["refs"] = std::move(arr);
        return r;
    });

    CROW_ROUTE(app, "/api/pota/spot").methods("POST"_method)
    ([&config, &decoder, &dialFreq](const crow::request& req) {
        crow::json::wvalue r;
        auto body = crow::json::load(req.body);
        if (!body || !body.has("reference")) { r["ok"] = false; r["error"] = "park reference required"; return r; }
        const std::string ref  = std::string(body["reference"].s());
        const std::string call = config.values().callsign;
        if (call.empty()) { r["ok"] = false; r["error"] = "set your callsign first"; return r; }

        const std::string m = decoder.mode();
        const std::string potaMode = (m == "FT8" || m == "FT4") ? m : "DATA";
        const long long dial = dialFreq.load();
        if (dial <= 0) { r["ok"] = false; r["error"] = "no rig frequency yet — try again in a moment"; return r; }
        char fk[24]; std::snprintf(fk, sizeof fk, "%.1f", dial / 1000.0);
        const std::string comments = body.has("comments") ? std::string(body["comments"].s())
                                                          : ("QRV " + potaMode);
        auto res = ft8web::PotaClient::spot(call, ref, fk, potaMode, comments);
        r["ok"] = res.ok;
        if (!res.ok) r["error"] = res.error;
        r["frequency"] = std::string(fk);
        r["mode"] = potaMode;
        return r;
    });

    CROW_ROUTE(app, "/api/logs/<string>/pota-upload").methods("POST"_method)
    ([&logs, &config, &notifyLogs](const std::string& file) {
        crow::json::wvalue r;
        auto parkOpt = logs.parkForFile(file);
        if (!parkOpt)          { r["ok"] = false; r["error"] = "no such log"; return r; }
        const std::string park = *parkOpt;
        if (park.empty())      { r["ok"] = false; r["error"] = "not a POTA log (no park reference)"; return r; }

        const auto cv = config.values();
        if (cv.potaUser.empty() || cv.potaPass.empty()) {
            r["ok"] = false; r["error"] = "set your POTA username/password in Station Config";
            return r;
        }

        auto pending = logs.potaPending(file);
        if (pending.empty()) {
            r["ok"] = true; r["uploaded"] = 0; r["message"] = "nothing new to upload";
            return r;
        }

        std::set<std::string> keys;
        for (auto& q : pending) {
            if (!q.count("station_callsign") && !cv.callsign.empty())
                q["station_callsign"] = cv.callsign;
            if (!q.count("operator"))
                q["operator"] = q.count("station_callsign") ? q["station_callsign"] : cv.callsign;
            q["my_sig"] = "POTA";
            q["my_sig_info"] = park;
            keys.insert(ft8web::LogStore::qsoKey(q));
        }

        const std::string adif = ft8web::LogStore::buildAdif(pending);
        auto up = ft8web::PotaClient::uploadAdif(cv.potaUser, cv.potaPass, adif);
        if (!up.ok) {
            std::fprintf(stderr, "[pota] upload failed: %s\n", up.error.c_str());
            r["ok"] = false;
            r["error"] = up.error;
            return r;
        }

        char ts[16];
        std::time_t now = std::time(nullptr);
        std::tm tmv{};
        std::strftime(ts, sizeof ts, "%Y%m%d", gmtime_r(&now, &tmv));
        const int marked = logs.markPotaUploaded(file, keys, ts);
        notifyLogs();
        r["ok"] = true;
        r["uploaded"] = marked;
        r["park"] = park;
        return r;
    });

    CROW_ROUTE(app, "/api/diag")
    ([&audio, &decoder, &uploader, &hub, &gps, &dialFreq, &txInhibit, &rigOkMs, serverStartMs] {
        crow::json::wvalue r;
        const long long now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        { std::ifstream f("/sys/class/thermal/thermal_zone0/temp"); long long t = 0;
          if (f >> t) r["cpu_temp_c"] = t / 1000.0; }
        { double l[3]; if (getloadavg(l, 3) == 3) { r["load1"] = l[0]; r["load5"] = l[1]; r["load15"] = l[2]; } }
        { std::ifstream f("/proc/meminfo"); std::string key, unit; long long val, total = 0, avail = 0;
          while (f >> key >> val >> unit) {
              if (key == "MemTotal:") total = val;
              else if (key == "MemAvailable:") { avail = val; break; }
          }
          if (total) { r["mem_total_mb"] = total / 1024; r["mem_avail_mb"] = avail / 1024; } }
        { struct statvfs s; if (statvfs(ft8web::ft8webLogs().c_str(), &s) == 0)
              r["disk_free_mb"] = static_cast<long long>(
                  static_cast<uint64_t>(s.f_bavail) * s.f_frsize / (1024 * 1024)); }
        { std::ifstream f("/proc/uptime"); double up = 0; if (f >> up) r["sys_uptime_s"] = static_cast<long long>(up); }
        r["proc_uptime_s"] = (now - serverStartMs) / 1000;

        r["rig_connected"] = (now - rigOkMs.load()) < 5000;
        r["rig_freq_hz"]   = dialFreq.load();
        r["tx_inhibit"]    = txInhibit.load();

        r["audio_running"]  = audio.running();
        r["audio_device"]   = audio.device();
        r["audio_level"]    = audio.level();
        r["audio_clipping"] = audio.clipping();
        r["audio_silent"]   = audio.silent();
        r["audio_error"]    = audio.error();
        r["audio_xruns"]    = static_cast<int64_t>(audio.xruns());
        r["clock_synced"]   = clockSynced();

        r["mode"] = decoder.mode();
        { auto stats = decoder.recentStats();
          std::vector<crow::json::wvalue> arr; double sum = 0, mx = 0;
          for (const auto& s : stats) {
              crow::json::wvalue e; e["mode"] = s.mode; e["wall_s"] = s.wall_s; e["count"] = s.count;
              arr.push_back(std::move(e)); sum += s.wall_s; if (s.wall_s > mx) mx = s.wall_s;
          }
          if (!stats.empty()) { r["decode_avg_s"] = sum / static_cast<double>(stats.size()); r["decode_max_s"] = mx; }
          r["decode_recent"] = std::move(arr); }

        { auto u = uploader.status();
          r["up_pending"] = u.pending; r["up_failed"] = u.failed; r["up_done"] = u.done; }

        r["ws_clients"] = static_cast<int>(hub.size());
        { auto gf = gps.fix(); r["gps_fix"] = gf.valid; }
        r["gps_grid"] = gps.grid();
        return r;
    });

    CROW_ROUTE(app, "/api/uploads")
    ([&uploader] {
        auto s = uploader.status();
        crow::json::wvalue r;
        r["lotw_enabled"] = s.lotwEnabled;
        r["qrz_enabled"]  = s.qrzEnabled;
        r["pending"] = s.pending;
        r["failed"]  = s.failed;
        r["done"]    = s.done;
        std::vector<crow::json::wvalue> arr;
        for (const auto& line : s.recent) arr.push_back(line);
        r["recent"] = std::move(arr);
        return r;
    });

    CROW_ROUTE(app, "/api/lookup/<string>")
    ([&qrz](const std::string& call) {
        crow::json::wvalue r;
        if (!qrz.enabled()) { r["ok"] = false; r["error"] = "QRZ lookup credentials not configured"; return r; }
        auto ci = qrz.lookup(call);
        r["ok"] = ci.ok;
        if (!ci.ok) { r["error"] = ci.error; return r; }
        r["call"]    = ci.call;
        r["fname"]   = ci.fname;
        r["name"]    = ci.name;
        r["addr1"]   = ci.addr1;
        r["addr2"]   = ci.addr2;
        r["state"]   = ci.state;
        r["zip"]     = ci.zip;
        r["country"] = ci.country;
        r["grid"]    = ci.grid;
        r["image"]   = ci.image;
        return r;
    });

    CROW_ROUTE(app, "/api/logs/<string>/adi")
    ([&logs](const crow::request&, crow::response& res, const std::string& file) {
        const std::string path = logs.rawPath(file);
        if (path.empty() || !std::filesystem::exists(path)) {
            res.code = 404; res.end(); return;
        }
        res.set_static_file_info_unsafe(path);
        res.set_header("Content-Type", "text/plain");
        res.set_header("Content-Disposition", "attachment; filename=\"" + logs.potaUploadName(file) + "\"");
        res.end();
    });

    CROW_ROUTE(app, "/api/logs/<string>/cbr")
    ([&logs](const crow::request&, crow::response& res, const std::string& file) {
        const std::string adi = logs.rawPath(file);
        if (adi.size() < 4) { res.code = 404; res.end(); return; }
        const std::string path = adi.substr(0, adi.size() - 4) + ".cbr";
        if (!std::filesystem::exists(path)) { res.code = 404; res.end(); return; }
        res.set_static_file_info_unsafe(path);
        res.set_header("Content-Type", "text/plain");
        res.set_header("Content-Disposition", "attachment; filename=\"" +
                       file.substr(0, file.size() - 4) + ".cbr\"");
        res.end();
    });

    CROW_ROUTE(app, "/api/offsets").methods("POST"_method)
    ([&decoder, &txOffset](const crow::request& req) {
        auto body = crow::json::load(req.body);
        if (body) {
            if (body.has("tx") && body["tx"].t() == crow::json::type::Number) {
                int tx = static_cast<int>(body["tx"].d());
                const bool wspr = decoder.mode() == "WSPR";

                tx = std::max(wspr ? 1403 : 200, std::min(wspr ? 1597 : 2800, tx));
                txOffset.store(tx);
            }
        }
        crow::json::wvalue r;
        r["rx"] = decoder.rxFreq();
        r["tx"] = txOffset.load();
        return r;
    });

    CROW_ROUTE(app, "/<path>")
    ([kWebRoot](const crow::request&, crow::response& res, std::string p) {
        if (p.rfind("api/", 0) == 0 || p.find("..") != std::string::npos) {
            res.code = 404; res.end(); return;
        }
        std::string full = kWebRoot + "/" + p;
        bool isIndex = false;
        if (!std::filesystem::exists(full) || std::filesystem::is_directory(full)) {
            full = kWebRoot + "/index.html"; isIndex = true;
        }
        res.set_static_file_info_unsafe(full);

        res.set_header("Cache-Control",
                       (isIndex || p == "index.html") ? "no-cache"
                                                      : "public, max-age=31536000, immutable");
        res.end();
    });

    app.bindaddr("0.0.0.0").port(8080).concurrency(16).run();

    decoder.stop();
    txe.disarm();
    audio.stopPlayback();
    rig.setPtt(false);

    pumpRun.store(false);
    if (pump.joinable()) pump.join();
    if (wfThread.joinable()) wfThread.join();
    return 0;
}
