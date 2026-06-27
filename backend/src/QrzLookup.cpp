// SPDX-License-Identifier: GPL-3.0-or-later

#include "QrzLookup.hpp"

#include <cstdio>
#include <algorithm>
#include <cctype>

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

std::string urlEncode(const std::string& s) {
    std::string o;
    char hex[8];
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') o += static_cast<char>(c);
        else { std::snprintf(hex, sizeof hex, "%%%02X", c); o += hex; }
    }
    return o;
}
}

void QrzLookup::configure(const std::string& user, const std::string& pass) {
    std::lock_guard<std::mutex> l(mtx_);
    if (user != user_ || pass != pass_) {
        sessionKey_.clear();
        cache_.clear();
    }
    user_ = user;
    pass_ = pass;
}

bool QrzLookup::enabled() const {
    std::lock_guard<std::mutex> l(mtx_);
    return !user_.empty() && !pass_.empty();
}

std::string QrzLookup::fetch(const std::string& url) {
    const std::string cmd = "curl -s --max-time 15 " + shellQuote(url) + " 2>/dev/null";
    FILE* pp = ::popen(cmd.c_str(), "r");
    if (!pp) return {};
    std::string out;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, pp)) > 0) {
        out.append(buf, n);
        if (out.size() > 1024 * 1024) break;
    }
    ::pclose(pp);
    return out;
}

std::string QrzLookup::xmlField(const std::string& xml, const std::string& tag) {
    const std::string open = "<" + tag + ">";
    auto p = xml.find(open);
    if (p == std::string::npos) return {};
    p += open.size();
    auto e = xml.find("</" + tag + ">", p);
    if (e == std::string::npos) return {};
    return xml.substr(p, e - p);
}

std::string QrzLookup::login(const std::string& user, const std::string& pass, std::string& err) {
    const std::string xml = fetch("https://xmldata.qrz.com/xml/current/?username=" + urlEncode(user) +
                                  ";password=" + urlEncode(pass) + ";agent=ft8web");
    const std::string key = xmlField(xml, "Key");
    if (key.empty()) {
        const std::string e = xmlField(xml, "Error");
        err = e.empty() ? "QRZ login failed (no response)" : ("QRZ: " + e);
    }
    return key;
}

CallInfo QrzLookup::lookup(const std::string& rawCall) {
    std::string call = rawCall;
    std::transform(call.begin(), call.end(), call.begin(),
                   [](unsigned char c) { return std::toupper(c); });

    std::string user, pass, key;
    {
        std::lock_guard<std::mutex> l(mtx_);
        auto it = cache_.find(call);
        if (it != cache_.end()) return it->second;
        user = user_; pass = pass_; key = sessionKey_;
    }

    CallInfo ci;
    ci.call = call;
    if (user.empty() || pass.empty()) { ci.error = "QRZ credentials not configured"; return ci; }

    for (int attempt = 0; attempt < 2; ++attempt) {
        if (key.empty()) {
            std::string err;
            key = login(user, pass, err);
            if (key.empty()) { ci.error = err; break; }
            std::lock_guard<std::mutex> l(mtx_);
            sessionKey_ = key;
        }
        const std::string xml = fetch("https://xmldata.qrz.com/xml/current/?s=" + key +
                                      ";callsign=" + urlEncode(call));
        const std::string e = xmlField(xml, "Error");
        if (!e.empty()) {
            if (e.find("Session Timeout") != std::string::npos ||
                e.find("Invalid session key") != std::string::npos) {
                {
                    std::lock_guard<std::mutex> l(mtx_);
                    if (sessionKey_ == key) sessionKey_.clear();
                }
                key.clear();
                if (attempt == 0) continue;
            }
            ci.error = "QRZ: " + e;
            break;
        }
        if (xml.find("<Callsign>") == std::string::npos) {
            ci.error = "QRZ: no response";
            break;
        }
        ci.ok      = true;
        ci.fname   = xmlField(xml, "fname");
        ci.name    = xmlField(xml, "name");
        ci.addr1   = xmlField(xml, "addr1");
        ci.addr2   = xmlField(xml, "addr2");
        ci.state   = xmlField(xml, "state");
        ci.zip     = xmlField(xml, "zip");
        ci.country = xmlField(xml, "country");
        ci.grid    = xmlField(xml, "grid");
        ci.image   = xmlField(xml, "image");
        break;
    }

    std::lock_guard<std::mutex> l(mtx_);
    if (cache_.size() > 500) cache_.clear();
    cache_[call] = ci;
    return ci;
}

}
