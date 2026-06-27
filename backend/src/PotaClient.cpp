// SPDX-License-Identifier: GPL-3.0-or-later

#include "PotaClient.hpp"

#include <cstdio>
#include <fstream>
#include <cstdlib>
#include <cmath>
#include <cctype>
#include <algorithm>
#include <vector>
#include <unistd.h>
#include <sys/stat.h>
#include "crow/json.h"
#include "HomeDir.hpp"

namespace ft8web {

namespace {

std::string shellQuote(const std::string& s) {
    std::string out = "'";
    for (char c : s) out += (c == '\'') ? "'\\''" : std::string(1, c);
    out += "'";
    return out;
}

std::string runCmd(const std::string& cmd) {
    FILE* pp = ::popen(cmd.c_str(), "r");
    if (!pp) return {};
    std::string out;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, pp)) > 0) {
        out.append(buf, n);
        if (out.size() > 8u * 1024 * 1024) break;
    }
    ::pclose(pp);
    return out;
}

std::string writeTemp(const std::string& prefix, const std::string& content, bool secret) {
    std::string tmpl = "/tmp/" + prefix + "XXXXXX";
    std::vector<char> buf(tmpl.begin(), tmpl.end());
    buf.push_back('\0');
    int fd = ::mkstemp(buf.data());
    if (fd < 0) return {};
    if (secret) ::fchmod(fd, 0600);
    const char* p = content.data();
    size_t left = content.size();
    while (left) {
        ssize_t n = ::write(fd, p, left);
        if (n <= 0) break;
        p += n; left -= static_cast<size_t>(n);
    }
    ::close(fd);
    return std::string(buf.data());
}

double distanceKm(double lat1, double lon1, double lat2, double lon2) {

    const double k = 111.32;
    const double dx = (lon2 - lon1) * std::cos((lat1 + lat2) / 2 * M_PI / 180.0) * k;
    const double dy = (lat2 - lat1) * k;
    return std::sqrt(dx * dx + dy * dy);
}

}

std::string PotaClient::httpGet(const std::string& url) {
    return runCmd("curl -s --max-time 15 -H 'User-Agent: ft8web/1.0' " + shellQuote(url) + " 2>/dev/null");
}

bool PotaClient::downloadParksCsv(const std::string& path, std::string& err) {
    const std::string tmp = path + ".tmp";
    runCmd("curl -fsS --max-time 120 -H 'User-Agent: ft8web/1.0' -o " + shellQuote(tmp) +
           " 'https://pota.app/all_parks_ext.csv' 2>/dev/null");
    auto rows = loadParksCsv(tmp);
    if (rows.size() < 1000) {
        ::unlink(tmp.c_str());
        err = "download failed or file implausibly small";
        return false;
    }
    if (std::rename(tmp.c_str(), path.c_str()) != 0) {
        ::unlink(tmp.c_str());
        err = "could not move parks file into place";
        return false;
    }
    return true;
}

static std::vector<std::string> csvFields(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    bool q = false;
    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (q) {
            if (c == '"' && i + 1 < line.size() && line[i + 1] == '"') { cur += '"'; ++i; }
            else if (c == '"') q = false;
            else cur += c;
        } else if (c == '"') q = true;
        else if (c == ',') { out.push_back(cur); cur.clear(); }
        else if (c != '\r') cur += c;
    }
    out.push_back(cur);
    return out;
}

std::vector<PotaClient::Park> PotaClient::loadParksCsv(const std::string& path) {

    std::vector<Park> out;
    std::ifstream f(path);
    if (!f) return out;
    std::string line;
    std::getline(f, line);
    while (std::getline(f, line)) {
        auto c = csvFields(line);
        if (c.size() < 7 || c[0].empty() || c[2] != "1") continue;
        Park p;
        p.reference = c[0];
        p.name = c[1];
        p.lat = std::atof(c[5].c_str());
        p.lon = std::atof(c[6].c_str());
        if (p.lat == 0.0 && p.lon == 0.0) continue;
        out.push_back(std::move(p));
    }
    return out;
}

std::vector<PotaClient::Park> PotaClient::parksInBoxLocal(const std::vector<Park>& db,
                                                          double south, double west,
                                                          double north, double east,
                                                          double clat, double clon, size_t max) {
    std::vector<Park> out;
    for (const auto& p : db) {
        if (p.lat < south || p.lat > north || p.lon < west || p.lon > east) continue;
        Park q = p;
        q.distKm = distanceKm(clat, clon, q.lat, q.lon);
        out.push_back(std::move(q));
    }
    std::sort(out.begin(), out.end(), [](const Park& a, const Park& b) { return a.distKm < b.distKm; });
    if (out.size() > max) out.resize(max);
    return out;
}

std::vector<PotaClient::Park> PotaClient::parksInBox(double south, double west, double north, double east,
                                                     double clat, double clon, size_t max) {
    std::vector<Park> out;
    char url[256];

    std::snprintf(url, sizeof url, "https://api.pota.app/park/grids/%.4f/%.4f/%.4f/%.4f/0",
                  south, west, north, east);
    const std::string body = httpGet(url);
    auto j = crow::json::load(body);
    if (!j || !j.has("features")) return out;

    for (const auto& f : j["features"]) {
        if (!f.has("properties") || !f.has("geometry")) continue;
        const auto& pr = f["properties"];
        const auto& gm = f["geometry"];
        if (!pr.has("reference") || !gm.has("coordinates")) continue;
        const auto& co = gm["coordinates"];
        if (co.size() < 2) continue;
        Park p;
        p.reference = std::string(pr["reference"].s());
        p.name = pr.has("name") ? std::string(pr["name"].s()) : p.reference;
        p.lon = co[0].d();
        p.lat = co[1].d();

        if (p.lat < south || p.lat > north || p.lon < west || p.lon > east) continue;
        p.distKm = distanceKm(clat, clon, p.lat, p.lon);
        out.push_back(std::move(p));
    }
    std::sort(out.begin(), out.end(), [](const Park& a, const Park& b) { return a.distKm < b.distKm; });
    if (out.size() > max) out.resize(max);
    return out;
}

PotaClient::Detail PotaClient::parkDetail(const std::string& reference) {
    Detail d;
    std::string ref;
    for (char c : reference)
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '/') ref += c;
    if (ref.empty()) return d;
    auto j = crow::json::load(httpGet("https://api.pota.app/park/" + ref));
    if (!j || !j.has("reference")) return d;
    auto str = [&](const char* k) {
        return (j.has(k) && j[k].t() == crow::json::type::String) ? std::string(j[k].s()) : std::string{};
    };
    d.ok           = true;
    d.reference    = str("reference");
    d.name         = str("name");
    d.grid         = !str("grid6").empty() ? str("grid6") : str("grid4");
    d.locationName = str("locationName");
    d.locationDesc = str("locationDesc");
    d.parktype     = str("parktypeDesc");
    d.website      = !str("website").empty() ? str("website") : str("parkURLs");
    if (j.has("latitude"))  d.lat = j["latitude"].d();
    if (j.has("longitude")) d.lon = j["longitude"].d();

    auto sj = crow::json::load(httpGet("https://api.pota.app/park/stats/" + ref));
    if (sj) {
        if (sj.has("activations")) d.activations = static_cast<int>(sj["activations"].i());
        if (sj.has("attempts"))    d.attempts    = static_cast<int>(sj["attempts"].i());
        if (sj.has("contacts"))    d.qsos        = static_cast<int>(sj["contacts"].i());
    }
    return d;
}

PotaClient::SpotResult PotaClient::spot(const std::string& activator, const std::string& reference,
                                        const std::string& freqKhz, const std::string& mode,
                                        const std::string& comments) {
    SpotResult r;
    if (activator.empty() || reference.empty()) { r.error = "callsign and park required"; return r; }

    crow::json::wvalue body;
    body["activator"] = activator;
    body["spotter"]   = activator;
    body["frequency"] = freqKhz;
    body["reference"] = reference;
    body["mode"]      = mode;
    body["source"]    = "ft8web";
    body["comments"]  = comments;
    const std::string payload = body.dump();

    const std::string cmd =
        "curl -s --max-time 15 -X POST "
        "-H 'Content-Type: application/json' -H 'User-Agent: ft8web/1.0' "
        "-H 'origin: https://pota.app' -H 'referer: https://pota.app/' "
        "-w '\\n%{http_code}' -d " + shellQuote(payload) +
        " 'https://api.pota.app/spot' 2>/dev/null";
    const std::string resp = runCmd(cmd);

    auto nl = resp.find_last_of('\n');
    const std::string code = nl != std::string::npos ? resp.substr(nl + 1) : "";
    const int status = std::atoi(code.c_str());
    if (status >= 200 && status < 300) {
        r.ok = true;
    } else {
        r.error = code.empty() ? "no response from POTA" : ("POTA returned HTTP " + code);
    }
    return r;
}

PotaClient::UploadResult PotaClient::uploadAdif(const std::string& user, const std::string& pass,
                                                const std::string& adif) {
    UploadResult r;
    if (user.empty() || pass.empty()) { r.error = "POTA username/password not set"; return r; }
    if (adif.empty())                 { r.error = "nothing to upload"; return r; }

    const char* env = ::getenv("FT8WEB_POTA_HELPER");
    const std::string helper = env ? std::string(env)
        : ft8web::ft8webRoot("backend/scripts/pota_api.py");

    const std::string pf = writeTemp("ft8web_pp_", pass, true);
    const std::string af = writeTemp("ft8web_pa_", adif, false);
    if (pf.empty() || af.empty()) {
        if (!pf.empty()) ::unlink(pf.c_str());
        if (!af.empty()) ::unlink(af.c_str());
        r.error = "could not create temp files";
        return r;
    }

    const std::string cmd =
        "timeout 90 python3 " + shellQuote(helper) +
        " --user " + shellQuote(user) +
        " --passfile " + shellQuote(pf) +
        " --adif " + shellQuote(af) + " 2>/dev/null";
    const std::string out = runCmd(cmd);
    ::unlink(pf.c_str());
    ::unlink(af.c_str());

    auto j = crow::json::load(out);
    if (!j || !j.has("ok")) {
        r.error = "POTA helper produced no result (python/requests available?)";
        r.detail = out.substr(0, 400);
        return r;
    }
    r.ok = j["ok"].b();
    if (j.has("status")) r.status = static_cast<int>(j["status"].i());
    if (!r.ok)
        r.error = j.has("error") ? std::string(j["error"].s())
                                 : ("POTA upload failed (HTTP " + std::to_string(r.status) + ")");
    r.detail = out;
    return r;
}

PotaClient::ActivationsResult PotaClient::userActivations(const std::string& user,
                                                          const std::string& pass) {
    ActivationsResult r;
    if (user.empty() || pass.empty()) { r.error = "POTA username/password not set"; return r; }

    const char* env = ::getenv("FT8WEB_POTA_HELPER");
    const std::string helper = env ? std::string(env)
        : ft8web::ft8webRoot("backend/scripts/pota_api.py");

    const std::string pf = writeTemp("ft8web_pp_", pass, true);
    if (pf.empty()) { r.error = "could not create temp file"; return r; }

    const std::string cmd =
        "timeout 45 python3 " + shellQuote(helper) +
        " --user " + shellQuote(user) +
        " --passfile " + shellQuote(pf) +
        " --activations 2>/dev/null";
    const std::string out = runCmd(cmd);
    ::unlink(pf.c_str());

    auto j = crow::json::load(out);
    if (!j || !j.has("ok")) { r.error = "POTA helper produced no result"; return r; }
    r.ok = j["ok"].b();
    if (!r.ok) {
        r.error = j.has("error") ? std::string(j["error"].s()) : "POTA activations fetch failed";
        return r;
    }
    if (j.has("refs"))
        for (const auto& e : j["refs"]) r.refs.push_back(std::string(e.s()));
    return r;
}

}
