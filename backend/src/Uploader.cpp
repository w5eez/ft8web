// SPDX-License-Identifier: GPL-3.0-or-later

#include "Uploader.hpp"

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <unistd.h>
#include <sys/wait.h>
#include "crow/json.h"

namespace ft8web {

namespace {
constexpr int kMaxRecent = 200;

std::string shellQuote(const std::string& s) {
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    out += "'";
    return out;
}
}

Uploader::Uploader(std::string queuePath) : queuePath_(std::move(queuePath)) {

    const auto slash = queuePath_.rfind('/');
    activityPath_ = (slash != std::string::npos ? queuePath_.substr(0, slash) : ".") + "/activity.json";

    load();
    thread_ = std::thread(&Uploader::loop, this);
}

Uploader::~Uploader() {
    run_.store(false);
    if (thread_.joinable()) thread_.join();
}

void Uploader::configure(const std::string& callsign, const std::string& grid,
                         bool lotwEnabled, const std::string& qrzApiKey, bool qrzEnabled) {
    std::lock_guard<std::mutex> l(cfgMtx_);
    call_   = callsign;
    grid_   = grid;
    qrzKey_ = qrzApiKey;
    lotwOn_.store(lotwEnabled && !callsign.empty());
    qrzOn_.store(qrzEnabled && !qrzApiKey.empty());
}

void Uploader::enqueue(QsoFields q) {
    if (!lotwOn_.load() && !qrzOn_.load()) return;
    LogStore::normalizeQsoMode(q);
    Item it;
    it.t = static_cast<long long>(std::time(nullptr));
    it.q = std::move(q);
    it.lotw = lotwOn_.load() ? "pending" : "skip";
    it.qrz  = qrzOn_.load()  ? "pending" : "skip";
    {
        std::lock_guard<std::mutex> l(itemsMtx_);
        it.id = nextId_++;
        items_.push_back(std::move(it));
        save();
    }
}

void Uploader::loop() {
    while (run_.load()) {
        for (int i = 0; i < 100 && run_.load(); ++i) usleep(100000);
        if (!run_.load()) break;

        const long long now = static_cast<long long>(std::time(nullptr));

        std::vector<Item> due;
        {
            std::lock_guard<std::mutex> l(itemsMtx_);
            for (const auto& it : items_)
                if ((it.lotw == "pending" || it.qrz == "pending") && it.nextTry <= now)
                    due.push_back(it);
        }
        for (auto& it : due) process(it);
        if (!due.empty()) {
            std::lock_guard<std::mutex> l(itemsMtx_);

            for (const auto& d : due) {
                for (auto& it : items_) {
                    if (it.id != d.id) continue;
                    it.lotw = d.lotw; it.qrz = d.qrz;
                    it.attempts = d.attempts; it.nextTry = d.nextTry;
                    break;
                }
            }

            if (items_.size() > 300) {
                items_.erase(std::remove_if(items_.begin(), items_.end(), [now](const Item& x) {
                    const bool settled = x.lotw != "pending" && x.qrz != "pending";
                    return settled && now - x.t > 86400;
                }), items_.end());
            }
            save();
        }
    }
}

void Uploader::process(Item& it) {
    const std::string who = it.q.count("call") ? it.q.at("call") : "?";
    bool transientFailure = false;

    if (it.lotw == "pending") {
        std::string err;
        bool permanent = false;
        if (uploadLotw(it, err, permanent)) {
            it.lotw = "done";
            note("LoTW ✓ " + who);
        } else if (permanent) {
            it.lotw = "failed:" + err;
            note("LoTW ✗ " + who + " — " + err);
        } else {
            transientFailure = true;
        }
    }
    if (it.qrz == "pending") {
        std::string err;
        bool permanent = false;
        if (uploadQrz(it, err, permanent)) {
            it.qrz = "done";
            note("QRZ ✓ " + who);
        } else if (permanent) {
            it.qrz = "failed:" + err;
            note("QRZ ✗ " + who + " — " + err);
        } else {
            transientFailure = true;
        }
    }
    if (transientFailure) {
        it.attempts++;
        const long long backoff = std::min<long long>(600, 30LL * (1LL << std::min(it.attempts, 5)));
        it.nextTry = static_cast<long long>(std::time(nullptr)) + backoff;
        if (it.attempts == 1 || it.attempts % 10 == 0)
            note("retrying " + who + " (attempt " + std::to_string(it.attempts) + ")");
    }
}

void Uploader::writeDynamicStationLocation(const std::string& gridOverride) const {
    std::string call, grid;
    {
        std::lock_guard<std::mutex> l(cfgMtx_);
        call = call_;
        grid = grid_;
    }
    if (!gridOverride.empty()) grid = gridOverride;

    auto xmlEsc = [](const std::string& s) {
        std::string o;
        for (char c : s) {
            switch (c) {
                case '&': o += "&amp;";  break;
                case '<': o += "&lt;";   break;
                case '>': o += "&gt;";   break;
                case '"': o += "&quot;"; break;
                case '\'': o += "&apos;"; break;
                default: if (static_cast<unsigned char>(c) >= 0x20) o += c;
            }
        }
        return o;
    };
    const char* home = ::getenv("HOME");
    std::string path = std::string(home ? home : "") + "/.tqsl/station_data";
    std::ofstream f(path, std::ios::trunc);
    if (!f) return;
    f << "<StationDataFile>\n<StationData name=\"FT8WEB\">\n"
      << "<CALL>" << xmlEsc(call) << "</CALL>\n"
      << "<DXCC>291</DXCC>\n"
      << "<GRIDSQUARE>" << xmlEsc(grid) << "</GRIDSQUARE>\n"
      << "</StationData>\n</StationDataFile>\n";
}

std::string Uploader::runCmd(const std::string& cmd, int& exitCode) {
    exitCode = -1;
    FILE* pp = ::popen(cmd.c_str(), "r");
    if (!pp) return {};
    std::string out;
    char buf[2048];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, pp)) > 0) {
        out.append(buf, n);
        if (out.size() > 256 * 1024) break;
    }
    int rc = ::pclose(pp);
    if (rc >= 0) exitCode = WEXITSTATUS(rc);
    return out;
}

bool Uploader::uploadLotw(const Item& it, std::string& err, bool& permanent) {

    const auto gi = it.q.find("my_gridsquare");
    writeDynamicStationLocation(gi != it.q.end() ? gi->second : std::string{});

    char tmpl[] = "/tmp/ft8web_lotw_XXXXXX";
    int fd = ::mkstemp(tmpl);
    if (fd < 0) { err = "tmpfile"; permanent = false; return false; }
    const std::string adi = tmpl;
    {
        const std::string doc = "ft8web LoTW upload\n<adif_ver:5>3.1.4 <programid:6>ft8web <eoh>\n"
                              + LogStore::recordToAdif(it.q);
        if (::write(fd, doc.data(), doc.size()) < 0) { ::close(fd); ::unlink(adi.c_str());
            err = "tmpfile"; permanent = false; return false; }
        ::close(fd);
    }
    int rc = -1;
    const std::string out = runCmd(
        "timeout 120 xvfb-run -a tqsl -x -d -a compliant -l FT8WEB -u " +
        shellQuote(adi) + " 2>&1", rc);
    ::unlink(adi.c_str());

    if (rc == 0 || rc == 9) return true;
    err = "tqsl exit " + std::to_string(rc);

    permanent = out.find("No certificate") != std::string::npos ||
                out.find("passphrase")     != std::string::npos ||
                out.find("not valid")      != std::string::npos ||
                out.find("Invalid station location") != std::string::npos;
    if (permanent) {
        auto nl = out.find('\n');
        err += ": " + out.substr(0, nl == std::string::npos ? 60 : std::min<size_t>(nl, 60));
    }
    return false;
}

bool Uploader::uploadQrz(const Item& it, std::string& err, bool& permanent) {
    std::string key;
    {
        std::lock_guard<std::mutex> l(cfgMtx_);
        key = qrzKey_;
    }
    const std::string record = LogStore::recordToAdif(it.q);
    int rc = -1;
    const std::string out = runCmd(
        "curl -s --max-time 25 --data " + shellQuote("KEY=" + key) +
        " --data ACTION=INSERT --data-urlencode " + shellQuote("ADIF=" + record) +
        " https://logbook.qrz.com/api 2>/dev/null", rc);

    if (out.find("RESULT=OK") != std::string::npos) return true;
    if (out.find("duplicate") != std::string::npos ||
        out.find("DUPLICATE") != std::string::npos) return true;
    if (out.empty() || rc != 0) { err = "network"; permanent = false; return false; }

    auto rp = out.find("REASON=");
    err = rp != std::string::npos ? out.substr(rp + 7, 60) : out.substr(0, 60);
    permanent = out.find("invalid api key") != std::string::npos ||
                out.find("AUTH") != std::string::npos ||
                rp != std::string::npos;
    return false;
}

Uploader::Status Uploader::status() const {
    Status s;
    s.lotwEnabled = lotwOn_.load();
    s.qrzEnabled  = qrzOn_.load();
    std::lock_guard<std::mutex> l(itemsMtx_);
    for (const auto& it : items_) {
        const bool pend = it.lotw == "pending" || it.qrz == "pending";
        const bool fail = it.lotw.rfind("failed", 0) == 0 || it.qrz.rfind("failed", 0) == 0;
        if (pend) s.pending++;
        else if (fail) s.failed++;
        else if (it.lotw == "done" || it.qrz == "done") s.done++;
    }
    s.recent.assign(recent_.begin(), recent_.end());
    return s;
}

void Uploader::note(const std::string& line) {
    std::fprintf(stderr, "[uploader] %s\n", line.c_str());
    const std::time_t tt = std::time(nullptr);
    std::tm tmv{};
    gmtime_r(&tt, &tmv);
    char ts[16];
    std::strftime(ts, sizeof ts, "%H:%M:%S", &tmv);

    std::deque<std::string> snap;
    {
        std::lock_guard<std::mutex> l(itemsMtx_);
        recent_.push_front(std::string(ts) + "  " + line);
        while (recent_.size() > kMaxRecent) recent_.pop_back();
        snap = recent_;
    }

    crow::json::wvalue arr = crow::json::wvalue::list();
    size_t i = 0;
    for (const auto& s : snap) arr[i++] = s;

    const std::string tmp = activityPath_ + ".tmp";
    {
        std::ofstream f(tmp, std::ios::trunc);
        if (!f) return;
        f << arr.dump() << "\n";
    }
    std::rename(tmp.c_str(), activityPath_.c_str());
}

void Uploader::activity(const std::string& line) { note(line); }

void Uploader::load() {
    {
        std::ifstream f(queuePath_);
        if (f) {
            std::stringstream ss;
            ss << f.rdbuf();
            auto j = crow::json::load(ss.str());
            if (j && j.t() == crow::json::type::List) {
                std::lock_guard<std::mutex> l(itemsMtx_);
                for (const auto& e : j) {
                    Item it;
                    if (e.has("id"))   it.id = e["id"].i();
                    if (e.has("t"))    it.t = e["t"].i();
                    if (e.has("lotw")) it.lotw = std::string(e["lotw"].s());
                    if (e.has("qrz"))  it.qrz = std::string(e["qrz"].s());
                    if (e.has("q") && e["q"].t() == crow::json::type::Object)
                        for (const auto& k : e["q"].keys())
                            if (e["q"][k].t() == crow::json::type::String)
                                it.q[k] = std::string(e["q"][k].s());
                    nextId_ = std::max(nextId_, it.id + 1);
                    items_.push_back(std::move(it));
                }
            }
        }
    }

    {
        std::ifstream af(activityPath_);
        if (af) {
            std::stringstream ss;
            ss << af.rdbuf();
            auto j = crow::json::load(ss.str());
            if (j && j.t() == crow::json::type::List) {
                std::lock_guard<std::mutex> l(itemsMtx_);
                for (const auto& e : j)
                    if (e.t() == crow::json::type::String)
                        recent_.push_back(std::string(e.s()));
                while (recent_.size() > kMaxRecent) recent_.pop_back();
            }
        }
    }
}

void Uploader::save() const {

    crow::json::wvalue arr = crow::json::wvalue::list();
    size_t i = 0;
    for (const auto& it : items_) {
        crow::json::wvalue e;
        e["id"]   = static_cast<int64_t>(it.id);
        e["t"]    = static_cast<int64_t>(it.t);
        e["lotw"] = it.lotw;
        e["qrz"]  = it.qrz;
        crow::json::wvalue q;
        for (const auto& kv : it.q) q[kv.first] = kv.second;
        e["q"] = std::move(q);
        arr[i++] = std::move(e);
    }

    const std::string tmp = queuePath_ + ".tmp";
    {
        std::ofstream f(tmp, std::ios::trunc);
        if (!f) return;
        f << arr.dump() << "\n";
    }
    std::rename(tmp.c_str(), queuePath_.c_str());
}

}
