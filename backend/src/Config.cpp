// SPDX-License-Identifier: GPL-3.0-or-later

#include "Config.hpp"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdint>
#include <sys/stat.h>
#include "crow/json.h"

namespace ft8web {

namespace {
std::string upperTrim(std::string s) {
    s.erase(0, s.find_first_not_of(" \t\r\n"));
    auto e = s.find_last_not_of(" \t\r\n");
    if (e != std::string::npos) s.erase(e + 1);
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::toupper(c); });
    return s;
}
std::string lowerTrim(std::string s) {
    s.erase(0, s.find_first_not_of(" \t\r\n"));
    auto e = s.find_last_not_of(" \t\r\n");
    if (e != std::string::npos) s.erase(e + 1);
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
}
}

Config::Config(std::string path) : path_(std::move(path)) {
    v_.opMode = "normal";
    load();

    bool seeded = false;
    {
        std::lock_guard<std::mutex> l(mtx_);
        if (v_.radios.empty()) {

            v_.radios.push_back({"FT-991A", 1035,
                "/dev/serial/by-id/usb-Silicon_Labs_CP2105_Dual_USB_to_UART_Bridge_Controller_XXXXXXXX-if00-port0",
                38400, "plughw:CARD=CODEC,DEV=0", 100.0});
            seeded = true;
        }
        if (v_.activeRadio < 0 || v_.activeRadio >= static_cast<int>(v_.radios.size())) v_.activeRadio = 0;
    }
    if (seeded) save();
}

std::string Config::callsign() const  { std::lock_guard<std::mutex> l(mtx_); return v_.callsign; }
std::string Config::grid() const      { std::lock_guard<std::mutex> l(mtx_); return v_.grid; }
bool        Config::autoGrid() const  { std::lock_guard<std::mutex> l(mtx_); return v_.autoGrid; }
bool        Config::pskEnabled() const{ std::lock_guard<std::mutex> l(mtx_); return v_.pskEnabled; }

Config::Values Config::values() const { std::lock_guard<std::mutex> l(mtx_); return v_; }

void Config::set(const Values& nv) {
    update([&](Values& v) { v = nv; });
}

void Config::update(const std::function<void(Values&)>& mut) {
    {
        std::lock_guard<std::mutex> l(mtx_);
        mut(v_);
        v_.callsign  = upperTrim(v_.callsign);
        v_.grid      = upperTrim(v_.grid);
        v_.fdClass   = upperTrim(v_.fdClass);
        v_.fdSection = upperTrim(v_.fdSection);
        v_.opMode    = lowerTrim(v_.opMode);
        if (v_.opMode != "pota" && v_.opMode != "fd") v_.opMode = "normal";
        v_.qsoRetries = std::max(1, std::min(20, v_.qsoRetries));
        if (v_.units != "mi") v_.units = "km";
        v_.txLevel    = std::max(0, std::min(100, v_.txLevel));
        v_.swrMax     = std::max(1.1, std::min(10.0, v_.swrMax));
    }
    save();
}

void Config::load() {
    std::ifstream f(path_);
    if (!f) return;
    std::stringstream ss;
    ss << f.rdbuf();
    auto j = crow::json::load(ss.str());
    if (!j) return;
    Values nv = v_;
    if (j.has("callsign"))    nv.callsign   = std::string(j["callsign"].s());
    if (j.has("grid"))        nv.grid       = std::string(j["grid"].s());
    if (j.has("psk_enabled")) nv.pskEnabled = j["psk_enabled"].b();
    if (j.has("op_mode"))     nv.opMode     = std::string(j["op_mode"].s());
    if (j.has("fd_class"))    nv.fdClass    = std::string(j["fd_class"].s());
    if (j.has("fd_section"))  nv.fdSection  = std::string(j["fd_section"].s());
    if (j.has("lotw_user"))   nv.lotwUser   = std::string(j["lotw_user"].s());
    if (j.has("lotw_pass"))   nv.lotwPass   = std::string(j["lotw_pass"].s());
    if (j.has("qrz_api_key")) nv.qrzApiKey  = std::string(j["qrz_api_key"].s());
    if (j.has("qrz_user"))    nv.qrzUser    = std::string(j["qrz_user"].s());
    if (j.has("qrz_pass"))    nv.qrzPass    = std::string(j["qrz_pass"].s());
    if (j.has("pota_user"))   nv.potaUser   = std::string(j["pota_user"].s());
    if (j.has("pota_pass"))   nv.potaPass   = std::string(j["pota_pass"].s());
    if (j.has("lotw_upload")) nv.lotwUpload = j["lotw_upload"].b();
    if (j.has("qrz_upload"))  nv.qrzUpload  = j["qrz_upload"].b();
    if (j.has("tx_even"))       nv.txEven       = j["tx_even"].b();
    if (j.has("auto_cq"))       nv.autoCq       = j["auto_cq"].b();
    if (j.has("qso_retries"))   nv.qsoRetries   = static_cast<int>(j["qso_retries"].i());
    if (j.has("max_cqs"))       nv.maxCqs       = static_cast<int>(j["max_cqs"].i());
    if (j.has("cq_skip"))       nv.cqSkip       = j["cq_skip"].b();
    if (j.has("find_clear_freq_before_cq")) nv.findClearFreqBeforeCq = j["find_clear_freq_before_cq"].b();
    if (j.has("tx_level"))      nv.txLevel      = static_cast<int>(j["tx_level"].i());
    if (j.has("swr_protect")) nv.swrProtect = j["swr_protect"].b();
    if (j.has("swr_max"))     nv.swrMax     = j["swr_max"].d();
    if (j.has("auto_grid"))   nv.autoGrid   = j["auto_grid"].b();
    if (j.has("units"))       nv.units      = std::string(j["units"].s());
    if (j.has("wf_gain"))     nv.wfGain     = j["wf_gain"].d();
    if (j.has("wf_palette"))  nv.wfPalette  = std::string(j["wf_palette"].s());
    if (j.has("show_calls"))  nv.showCalls  = j["show_calls"].b();
    if (j.has("active_radio")) nv.activeRadio = static_cast<int>(j["active_radio"].i());
    if (j.has("radio_mode"))    nv.radioMode   = std::string(j["radio_mode"].s());
    if (j.has("radio_dial_hz")) nv.radioDialHz = j["radio_dial_hz"].i();
    if (j.has("radios") && j["radios"].t() == crow::json::type::List) {
        nv.radios.clear();
        bool sawModel = false;
        try {
            for (const auto& rj : j["radios"]) {
                Radio rd;
                if (rj.has("name"))        rd.name      = std::string(rj["name"].s());
                if (rj.has("model"))     { rd.model     = static_cast<int>(rj["model"].i()); sawModel = true; }
                if (rj.has("serial"))      rd.serial    = std::string(rj["serial"].s());
                if (rj.has("baud"))        rd.baud      = static_cast<int>(rj["baud"].i());
                if (rj.has("device"))      rd.device    = std::string(rj["device"].s());
                if (rj.has("power_max_w")) rd.powerMaxW = rj["power_max_w"].d();
                if (rj.has("data_mode"))   rd.dataMode  = std::string(rj["data_mode"].s());
                if (rj.has("rx_channel"))  rd.rxChannel = static_cast<int>(rj["rx_channel"].i());
                if (rj.has("rx_filter_hz")) rd.rxFilterHz = static_cast<int>(rj["rx_filter_hz"].i());
                nv.radios.push_back(std::move(rd));
            }
        } catch (...) { nv.radios.clear(); }
        if (!sawModel) nv.radios.clear();
    }
    set(nv);
}

void Config::save() const {
    crow::json::wvalue j;
    {
        std::lock_guard<std::mutex> l(mtx_);
        j["callsign"]    = v_.callsign;
        j["grid"]        = v_.grid;
        j["psk_enabled"] = v_.pskEnabled;
        j["op_mode"]     = v_.opMode;
        j["fd_class"]    = v_.fdClass;
        j["fd_section"]  = v_.fdSection;
        j["lotw_user"]   = v_.lotwUser;
        j["lotw_pass"]   = v_.lotwPass;
        j["qrz_api_key"] = v_.qrzApiKey;
        j["qrz_user"]    = v_.qrzUser;
        j["qrz_pass"]    = v_.qrzPass;
        j["pota_user"]   = v_.potaUser;
        j["pota_pass"]   = v_.potaPass;
        j["lotw_upload"] = v_.lotwUpload;
        j["qrz_upload"]  = v_.qrzUpload;
        j["tx_even"]       = v_.txEven;
        j["auto_cq"]       = v_.autoCq;
        j["qso_retries"]   = v_.qsoRetries;
        j["max_cqs"]       = v_.maxCqs;
        j["cq_skip"]       = v_.cqSkip;
        j["find_clear_freq_before_cq"] = v_.findClearFreqBeforeCq;
        j["tx_level"]      = v_.txLevel;
        j["swr_protect"] = v_.swrProtect;
        j["swr_max"]     = v_.swrMax;
        j["auto_grid"]   = v_.autoGrid;
        j["units"]       = v_.units;
        j["wf_gain"]     = v_.wfGain;
        j["wf_palette"]  = v_.wfPalette;
        j["show_calls"]  = v_.showCalls;
        j["active_radio"] = v_.activeRadio;
        j["radio_mode"]    = v_.radioMode;
        j["radio_dial_hz"] = static_cast<std::int64_t>(v_.radioDialHz);
        std::vector<crow::json::wvalue> radioList;
        for (const auto& rd : v_.radios) {
            crow::json::wvalue rj;
            rj["name"]        = rd.name;
            rj["model"]       = rd.model;
            rj["serial"]      = rd.serial;
            rj["baud"]        = rd.baud;
            rj["device"]      = rd.device;
            rj["power_max_w"] = rd.powerMaxW;
            rj["data_mode"]   = rd.dataMode;
            rj["rx_channel"]  = rd.rxChannel;
            rj["rx_filter_hz"] = rd.rxFilterHz;
            radioList.push_back(std::move(rj));
        }
        j["radios"] = std::move(radioList);
    }

    const std::string tmp = path_ + ".tmp";
    {
        std::ofstream f(tmp);
        if (!f) return;
        f << j.dump() << "\n";
    }
    ::chmod(tmp.c_str(), 0600);
    std::rename(tmp.c_str(), path_.c_str());
}

}
