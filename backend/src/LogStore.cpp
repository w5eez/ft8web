// SPDX-License-Identifier: GPL-3.0-or-later

#include "LogStore.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <ctime>
#include "crow/json.h"

namespace fs = std::filesystem;

namespace ft8web {

namespace {

std::string slugify(const std::string& name) {
    std::string s;
    for (char c : name) {
        const char l = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if ((l >= 'a' && l <= 'z') || (l >= '0' && l <= '9')) s += l;
        else if (!s.empty() && s.back() != '-') s += '-';
    }
    while (!s.empty() && s.back() == '-') s.pop_back();
    return s.empty() ? "log" : s;
}

std::vector<QsoFields> parseAdif(const std::string& text) {
    std::vector<QsoFields> out;
    QsoFields cur;

    static const std::string kEoh = "<eoh>";
    const bool hasEoh =
        std::search(text.begin(), text.end(), kEoh.begin(), kEoh.end(),
                    [](char a, char b) {
                        return std::tolower(static_cast<unsigned char>(a)) == static_cast<unsigned char>(b);
                    }) != text.end();
    bool inHeader = hasEoh;
    size_t i = 0;
    while ((i = text.find('<', i)) != std::string::npos) {
        size_t e = text.find('>', i);
        if (e == std::string::npos) break;
        std::string tag = text.substr(i + 1, e - i - 1);
        i = e + 1;
        std::string lower = tag;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        if (lower == "eoh") { inHeader = false; continue; }
        if (lower == "eor") {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
            continue;
        }
        auto c1 = lower.find(':');
        if (c1 == std::string::npos) continue;
        std::string fname = lower.substr(0, c1);
        auto c2 = lower.find(':', c1 + 1);
        int len = std::atoi(lower.substr(c1 + 1, (c2 == std::string::npos ? lower.size() : c2) - c1 - 1).c_str());
        if (len < 0) len = 0;
        std::string value = text.substr(i, static_cast<size_t>(len));
        i += static_cast<size_t>(len);
        if (!inHeader) cur[fname] = value;
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

const char* kOrder[] = {
    "call", "qso_date", "time_on", "qso_date_off", "time_off", "band", "freq", "mode", "submode",
    "rst_sent", "rst_rcvd", "gridsquare", "station_callsign", "my_gridsquare",
    "my_sig", "my_sig_info", "comment",
};

bool validFieldName(const std::string& k) {
    if (k.empty()) return false;
    for (char c : k)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
    return true;
}

std::string writeRecord(const QsoFields& q) {
    std::ostringstream os;
    auto emit = [&](const std::string& k, const std::string& v) {
        if (v.empty() || !validFieldName(k)) return;
        std::string up = k, val = v;
        std::transform(up.begin(), up.end(), up.begin(),
                       [](unsigned char c) { return std::toupper(c); });
        if (up == "BAND")
            std::transform(val.begin(), val.end(), val.begin(),
                           [](unsigned char c) { return std::toupper(c); });
        os << '<' << up << ':' << val.size() << '>' << val << ' ';
    };
    QsoFields rest = q;
    for (const char* k : kOrder) {
        auto it = rest.find(k);
        if (it != rest.end()) { emit(k, it->second); rest.erase(it); }
    }
    for (const auto& kv : rest) emit(kv.first, kv.second);
    os << "<EOR>\n";
    return os.str();
}

const char kHeader[] = "<ADIF_VER:5>3.1.4 <PROGRAMID:6>ft8web <EOH>\n";

const char kPotaMark[] = "app_ft8web_potaup";

}

LogStore::LogStore(std::string dir) : dir_(std::move(dir)) {
    std::error_code ec;
    fs::create_directories(dir_, ec);
    loadMeta();
}

std::string LogStore::recordToAdif(const QsoFields& q) { return writeRecord(q); }
std::vector<QsoFields> LogStore::parseAdifRecords(const std::string& text) { return parseAdif(text); }
void LogStore::normalizeQsoMode(QsoFields& q) { normalizeMode(q); }

bool LogStore::validFile(const std::string& file) {
    if (file.size() < 5 || file.size() > 80) return false;
    if (file.substr(file.size() - 4) != ".adi") return false;
    for (char c : file.substr(0, file.size() - 4))
        if (!std::islower(static_cast<unsigned char>(c)) &&
            !std::isdigit(static_cast<unsigned char>(c)) && c != '-' && c != '_') return false;
    return true;
}

std::string LogStore::adifPath(const std::string& file) const { return dir_ + "/" + file; }
std::string LogStore::rawPath(const std::string& file) const {
    return validFile(file) ? adifPath(file) : std::string{};
}

void LogStore::loadMeta() {
    std::lock_guard<std::mutex> l(mtx_);
    meta_.clear();
    std::ifstream f(dir_ + "/meta.json");
    if (f) {
        std::stringstream ss;
        ss << f.rdbuf();
        auto j = crow::json::load(ss.str());
        if (j && j.t() == crow::json::type::Object) {
            if (j.has("__counter__")) counter_ = static_cast<int>(j["__counter__"].i());
            for (const auto& key : j.keys()) {
                if (!validFile(key)) continue;
                Meta m;
                const auto& e = j[key];
                if (e.has("name"))    m.name    = std::string(e["name"].s());
                if (e.has("park"))    m.park    = std::string(e["park"].s());
                if (e.has("active"))  m.active  = e["active"].b();
                if (e.has("created")) m.created = e["created"].i();
                meta_[key] = std::move(m);
            }
        }
    }

    std::error_code ec;
    for (const auto& de : fs::directory_iterator(dir_, ec)) {
        const std::string fn = de.path().filename().string();
        if (validFile(fn) && !meta_.count(fn)) {
            Meta m;
            m.name = fn.substr(0, fn.size() - 4);
            m.created = static_cast<long long>(std::time(nullptr));
            meta_[fn] = std::move(m);
        }
    }
}

void LogStore::saveMeta() const {
    crow::json::wvalue j;
    j["__counter__"] = counter_;
    for (const auto& kv : meta_) {
        crow::json::wvalue e;
        e["name"]    = kv.second.name;
        e["park"]    = kv.second.park;
        e["active"]  = kv.second.active;
        e["created"] = static_cast<int64_t>(kv.second.created);
        j[kv.first] = std::move(e);
    }

    const std::string path = dir_ + "/meta.json";
    const std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp);
        if (!f) return;
        f << j.dump() << "\n";
    }
    std::error_code ec;
    fs::rename(tmp, path, ec);
}

int LogStore::qsoCounter() const {
    std::lock_guard<std::mutex> l(mtx_);
    return counter_;
}

void LogStore::bumpCounter() {
    std::lock_guard<std::mutex> l(mtx_);
    ++counter_;
    saveMeta();
}

void LogStore::resetCounter() {
    std::lock_guard<std::mutex> l(mtx_);
    counter_ = 0;
    saveMeta();
}

std::vector<LogInfo> LogStore::list() {
    std::lock_guard<std::mutex> l(mtx_);
    std::vector<LogInfo> out;
    for (const auto& kv : meta_) {
        LogInfo li;
        li.file = kv.first;
        li.name = kv.second.name;
        li.park = kv.second.park;
        li.active = kv.second.active;
        li.created = kv.second.created;
        std::ifstream f(adifPath(kv.first));
        if (f) {
            std::stringstream ss;
            ss << f.rdbuf();
            auto recs = parseAdif(ss.str());
            li.qsos = static_cast<int>(recs.size());
            for (const auto& r : recs) {
                auto it = r.find(kPotaMark);
                if (it != r.end() && !it->second.empty()) ++li.potaUploaded;
            }
        }
        out.push_back(std::move(li));
    }
    std::sort(out.begin(), out.end(),
              [](const LogInfo& a, const LogInfo& b) { return a.created > b.created; });
    return out;
}

std::optional<std::string> LogStore::parkForFile(const std::string& file) {
    if (!validFile(file)) return std::nullopt;
    std::lock_guard<std::mutex> l(mtx_);
    auto it = meta_.find(file);
    if (it == meta_.end()) return std::nullopt;
    return it->second.park;
}

std::optional<LogInfo> LogStore::create(const std::string& name, const std::string& park, bool active) {
    std::lock_guard<std::mutex> l(mtx_);
    std::string base = slugify(name.empty() ? park : name);
    std::string file = base + ".adi";
    for (int n = 2; meta_.count(file) || fs::exists(adifPath(file)); ++n)
        file = base + "-" + std::to_string(n) + ".adi";

    std::ofstream f(adifPath(file));
    if (!f) return std::nullopt;
    f << kHeader;
    f.close();

    Meta m;
    m.name = name.empty() ? base : name;
    m.park = park;
    m.active = active;
    m.created = static_cast<long long>(std::time(nullptr));
    meta_[file] = m;
    saveMeta();

    LogInfo li;
    li.file = file; li.name = m.name; li.park = m.park;
    li.active = m.active; li.created = m.created; li.qsos = 0;
    return li;
}

bool LogStore::remove(const std::string& file) {
    if (!validFile(file)) return false;
    std::lock_guard<std::mutex> l(mtx_);
    std::error_code ec;
    fs::remove(adifPath(file), ec);
    fs::remove(cabrilloPath(file), ec);
    meta_.erase(file);
    saveMeta();
    return true;
}

bool LogStore::setMeta(const std::string& file, const std::string* name,
                       const std::string* park, const bool* active) {
    if (!validFile(file)) return false;
    std::lock_guard<std::mutex> l(mtx_);
    auto it = meta_.find(file);
    if (it == meta_.end()) return false;
    if (name)   it->second.name   = *name;
    if (park)   it->second.park   = *park;
    if (active) it->second.active = *active;
    saveMeta();
    return true;
}

std::vector<QsoFields> LogStore::qsos(const std::string& file) {
    if (!validFile(file)) return {};
    std::lock_guard<std::mutex> l(mtx_);
    std::ifstream f(adifPath(file));
    if (!f) return {};
    std::stringstream ss;
    ss << f.rdbuf();
    return parseAdif(ss.str());
}

void LogStore::normalizeMode(QsoFields& q) {
    auto it = q.find("mode");
    if (it == q.end()) return;
    std::string m = it->second;
    std::transform(m.begin(), m.end(), m.begin(),
                   [](unsigned char c) { return std::toupper(c); });
    if (m == "FT4") {
        if (!q.count("submode")) q["submode"] = m;
        it->second = "MFSK";
    } else if (m == "FT2") {
        it->second = "MFSK";
    }
}

QsoFields LogStore::stamped(QsoFields q, const Meta& m) const {
    normalizeMode(q);
    if (!m.park.empty()) {
        q["my_sig"] = "POTA";
        q["my_sig_info"] = m.park;
    }
    if (!stationCall_.empty() && !q.count("station_callsign"))
        q["station_callsign"] = stationCall_;
    return q;
}

void LogStore::setStationCallsign(const std::string& call) {
    std::lock_guard<std::mutex> l(mtx_);
    stationCall_ = call;
}

bool LogStore::addQso(const std::string& file, QsoFields q) {
    if (!validFile(file)) return false;
    std::lock_guard<std::mutex> l(mtx_);
    auto it = meta_.find(file);
    if (it == meta_.end()) return false;
    {
        std::ofstream f(adifPath(file), std::ios::app);
        if (!f) return false;
        f << writeRecord(stamped(std::move(q), it->second));
    }
    writeCabrillo(file);
    return true;
}

QsoFields LogStore::stampForUpload(QsoFields q) const {
    std::lock_guard<std::mutex> l(mtx_);
    if (!stationCall_.empty() && !q.count("station_callsign"))
        q["station_callsign"] = stationCall_;
    for (const auto& kv : meta_)
        if (kv.second.active && !kv.second.park.empty()) {
            q["my_sig"] = "POTA";
            q["my_sig_info"] = kv.second.park;
            break;
        }
    return q;
}

QsoFields LogStore::stampForFile(const std::string& file, QsoFields q) const {
    std::lock_guard<std::mutex> l(mtx_);
    if (!stationCall_.empty() && !q.count("station_callsign"))
        q["station_callsign"] = stationCall_;
    auto it = meta_.find(file);
    if (it != meta_.end() && !it->second.park.empty()) {
        q["my_sig"] = "POTA";
        q["my_sig_info"] = it->second.park;
    }
    return q;
}

int LogStore::addQsoToActive(QsoFields q) {
    std::lock_guard<std::mutex> l(mtx_);
    int n = 0;
    for (const auto& kv : meta_) {
        if (!kv.second.active) continue;
        {
            std::ofstream f(adifPath(kv.first), std::ios::app);
            if (!f) continue;
            f << writeRecord(stamped(q, kv.second));
        }
        writeCabrillo(kv.first);
        ++n;
    }
    return n;
}

bool LogStore::writeAll(const std::string& file, const std::vector<QsoFields>& records) {

    const std::string path = adifPath(file);
    const std::string tmp  = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::trunc);
        if (!f) return false;
        f << kHeader;
        for (const auto& r : records) f << writeRecord(r);
        if (!f) return false;
    }
    std::error_code ec;
    fs::rename(tmp, path, ec);
    if (ec) { fs::remove(tmp, ec); return false; }
    writeCabrillo(file);
    return true;
}

std::string LogStore::cabrilloPath(const std::string& file) const {

    return dir_ + "/" + file.substr(0, file.size() - 4) + ".cbr";
}

std::string LogStore::buildCabrillo(const std::vector<QsoFields>& records,
                                    const std::string& callsign) {
    auto get = [](const QsoFields& q, const char* k) -> std::string {
        auto it = q.find(k); return it == q.end() ? std::string() : it->second;
    };
    std::string call = callsign, grid;
    if (!records.empty()) {
        if (std::string c = get(records.front(), "station_callsign"); !c.empty()) call = c;
        grid = get(records.front(), "my_gridsquare");
    }
    std::ostringstream os;
    os << "START-OF-LOG: 3.0\n"
       << "CREATED-BY: ft8web\n"
       << "CALLSIGN: " << call << '\n'
       << "CONTEST: OPEN\n"
       << "CATEGORY-OPERATOR: SINGLE-OP\n"
       << "CATEGORY-MODE: DIGI\n"
       << "CATEGORY-BAND: ALL\n";
    if (!grid.empty()) os << "GRID-LOCATOR: " << grid << '\n';
    if (!call.empty()) os << "OPERATORS: " << call << '\n';
    for (const auto& q : records) {
        long khz = 0;
        if (std::string f = get(q, "freq"); !f.empty()) {
            try { khz = std::lround(std::stod(f) * 1000.0); } catch (...) { khz = 0; }
        }
        const std::string m = get(q, "mode");
        std::string cm = "DG";
        if (m == "CW") cm = "CW";
        else if (m == "SSB" || m == "USB" || m == "LSB" || m == "PH") cm = "PH";
        else if (m == "FM") cm = "FM";
        else if (m == "RTTY") cm = "RY";
        const std::string d = get(q, "qso_date");
        const std::string date = d.size() == 8 ? d.substr(0,4) + "-" + d.substr(4,2) + "-" + d.substr(6,2) : d;
        const std::string t = get(q, "time_on");
        const std::string time = t.size() >= 4 ? t.substr(0,4) : t;
        std::string myc = get(q, "station_callsign"); if (myc.empty()) myc = call;
        char line[320];
        std::snprintf(line, sizeof line, "QSO: %6ld %-2s %-10s %-4s %-13s %-6s %-13s %-6s\n",
                      khz, cm.c_str(), date.c_str(), time.c_str(),
                      myc.c_str(), get(q, "rst_sent").c_str(),
                      get(q, "call").c_str(), get(q, "rst_rcvd").c_str());
        os << line;
    }
    os << "END-OF-LOG:\n";
    return os.str();
}

void LogStore::writeCabrillo(const std::string& file) {

    std::ifstream in(adifPath(file));
    if (!in) return;
    std::stringstream ss;
    ss << in.rdbuf();
    in.close();
    const std::string text = buildCabrillo(parseAdif(ss.str()), stationCall_);
    const std::string path = cabrilloPath(file);
    const std::string tmp  = path + ".tmp";
    { std::ofstream o(tmp, std::ios::trunc); if (!o) return; o << text; if (!o) return; }
    std::error_code ec;
    fs::rename(tmp, path, ec);
    if (ec) fs::remove(tmp, ec);
}

bool LogStore::updateQso(const std::string& file, size_t idx, const QsoFields& q) {
    if (!validFile(file)) return false;
    std::lock_guard<std::mutex> l(mtx_);
    std::ifstream f(adifPath(file));
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    auto records = parseAdif(ss.str());
    if (idx >= records.size()) return false;
    records[idx] = q;
    normalizeMode(records[idx]);
    return writeAll(file, records);
}

bool LogStore::deleteQso(const std::string& file, size_t idx) {
    if (!validFile(file)) return false;
    std::lock_guard<std::mutex> l(mtx_);
    std::ifstream f(adifPath(file));
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    auto records = parseAdif(ss.str());
    if (idx >= records.size()) return false;
    records.erase(records.begin() + static_cast<long>(idx));
    return writeAll(file, records);
}

std::string LogStore::qsoKey(const QsoFields& q) {
    auto g = [&](const char* k) {
        auto it = q.find(k);
        return it == q.end() ? std::string() : it->second;
    };
    return g("call") + "|" + g("qso_date") + "|" + g("time_on") + "|" + g("band") + "|" + g("mode");
}

std::string LogStore::buildAdif(const std::vector<QsoFields>& records) {
    std::string out = kHeader;
    for (const auto& r : records) out += writeRecord(r);
    return out;
}

std::string LogStore::potaUploadName(const std::string& file) {
    if (!validFile(file)) return file;
    std::lock_guard<std::mutex> l(mtx_);
    auto it = meta_.find(file);
    if (it == meta_.end() || it->second.park.empty()) return file;
    const std::string park = it->second.park;
    std::string call = stationCall_;
    std::ifstream f(adifPath(file));
    if (!f) return file;
    std::stringstream ss;
    ss << f.rdbuf();
    std::string date;
    for (const auto& r : parseAdif(ss.str())) {
        if (call.empty()) {
            auto c = r.find("station_callsign");
            if (c != r.end()) call = c->second;
        }
        auto d = r.find("qso_date");
        if (d != r.end() && (date.empty() || d->second < date)) date = d->second;
    }
    if (call.empty() || date.empty()) return file;
    return call + "@" + park + "-" + date + ".adi";
}

std::vector<QsoFields> LogStore::potaPending(const std::string& file) {
    if (!validFile(file)) return {};
    std::lock_guard<std::mutex> l(mtx_);
    std::ifstream f(adifPath(file));
    if (!f) return {};
    std::stringstream ss;
    ss << f.rdbuf();
    std::vector<QsoFields> out;
    for (auto& r : parseAdif(ss.str())) {
        auto it = r.find(kPotaMark);
        if (it == r.end() || it->second.empty()) out.push_back(std::move(r));
    }
    return out;
}

int LogStore::markPotaUploaded(const std::string& file, const std::set<std::string>& keys,
                               const std::string& stamp) {
    if (!validFile(file)) return 0;
    std::lock_guard<std::mutex> l(mtx_);
    std::ifstream f(adifPath(file));
    if (!f) return 0;
    std::stringstream ss;
    ss << f.rdbuf();
    auto records = parseAdif(ss.str());
    int n = 0;
    for (auto& r : records) {
        auto it = r.find(kPotaMark);
        if (it != r.end() && !it->second.empty()) continue;
        if (keys.count(qsoKey(r))) { r[kPotaMark] = stamp; ++n; }
    }
    if (n) writeAll(file, records);
    return n;
}

}
