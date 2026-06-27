// SPDX-License-Identifier: GPL-3.0-or-later

#include "RigClient.hpp"

#include <hamlib/rig.h>
#include <cstdio>
#include <cstring>
#include <chrono>
#include <mutex>

namespace ft8web {

namespace {

long long nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

void hamlibInitOnce() {
    static std::once_flag once;
    std::call_once(once, [] {
        rig_set_debug(RIG_DEBUG_NONE);
        rig_load_all_backends();
    });
}

}

RigClient::~RigClient() {
    std::lock_guard<std::mutex> lk(mtx_);
    closeRig();
}

void RigClient::closeRig() {
    if (rig_) {
        if (open_.load()) rig_close(rig_);
        rig_cleanup(rig_);
        rig_ = nullptr;
    }
    open_.store(false);
}

void RigClient::setRadio(int model, const std::string& device, int baud, double powerMaxW) {
    std::lock_guard<std::mutex> lk(mtx_);
    closeRig();
    model_    = model;
    device_   = device;
    baud_     = baud > 0 ? baud : 38400;
    wantOpen_ = (model != 0 && !device.empty());
    powerScale_.store(powerMaxW > 0 ? powerMaxW : 100.0);
    lastOpenMs_ = 0;
    ensureOpen();
}

bool RigClient::ensureOpen() {
    if (open_.load()) return true;
    if (!wantOpen_) return false;

    const long long t = nowMs();
    if (t - lastOpenMs_ < 5000) return false;
    lastOpenMs_ = t;

    hamlibInitOnce();
    if (!rig_) {
        rig_ = rig_init(static_cast<rig_model_t>(model_));
        if (!rig_) { std::fprintf(stderr, "[rig] rig_init(%d) failed\n", model_); return false; }
        rig_set_conf(rig_, rig_token_lookup(rig_, "rig_pathname"), device_.c_str());
        char b[16]; std::snprintf(b, sizeof b, "%d", baud_);
        rig_set_conf(rig_, rig_token_lookup(rig_, "serial_speed"), b);
    }
    const int e = rig_open(rig_);
    if (e != RIG_OK) {
        std::fprintf(stderr, "[rig] open model %d on %s @ %d failed: %s\n",
                     model_, device_.c_str(), baud_, rigerror(e));
        return false;
    }
    open_.store(true);
    std::fprintf(stderr, "[rig] opened: model %d, %s @ %d\n", model_, device_.c_str(), baud_);
    return true;
}

bool RigClient::checkComm(int err) {
    if (err == RIG_OK) return true;

    if (err == -RIG_EIO || err == -RIG_ETIMEOUT || err == -RIG_EPROTO) {
        if (rig_ && open_.load()) rig_close(rig_);
        open_.store(false);
    }
    return false;
}

std::optional<long long> RigClient::getFrequency() {
    std::lock_guard<std::mutex> lk(mtx_);
    if (!ensureOpen()) return std::nullopt;
    freq_t f = 0;
    if (!checkComm(rig_get_freq(rig_, RIG_VFO_CURR, &f))) return std::nullopt;
    return static_cast<long long>(f);
}

bool RigClient::setFrequency(long long hz) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (!ensureOpen()) return false;
    return checkComm(rig_set_freq(rig_, RIG_VFO_CURR, static_cast<freq_t>(hz)));
}

std::optional<std::pair<std::string,int>> RigClient::getMode() {
    std::lock_guard<std::mutex> lk(mtx_);
    if (!ensureOpen()) return std::nullopt;
    rmode_t m = RIG_MODE_NONE;
    pbwidth_t w = 0;
    if (!checkComm(rig_get_mode(rig_, RIG_VFO_CURR, &m, &w))) return std::nullopt;
    return std::make_pair(std::string(rig_strrmode(m)), static_cast<int>(w));
}

bool RigClient::setMode(const std::string& mode, int passband) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (!ensureOpen()) return false;
    const rmode_t m = rig_parse_mode(mode.c_str());
    if (m == RIG_MODE_NONE) return false;

    const pbwidth_t w = passband > 0 ? static_cast<pbwidth_t>(passband)
                      : passband < 0 ? RIG_PASSBAND_NOCHANGE
                                     : RIG_PASSBAND_NORMAL;
    return checkComm(rig_set_mode(rig_, RIG_VFO_CURR, m, w));
}

std::optional<bool> RigClient::getPtt() {
    std::lock_guard<std::mutex> lk(mtx_);
    if (!ensureOpen()) return std::nullopt;
    ptt_t p = RIG_PTT_OFF;
    if (!checkComm(rig_get_ptt(rig_, RIG_VFO_CURR, &p))) return std::nullopt;
    return p != RIG_PTT_OFF;
}

bool RigClient::setPtt(bool on) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (!ensureOpen()) return false;
    return checkComm(rig_set_ptt(rig_, RIG_VFO_CURR, on ? RIG_PTT_ON : RIG_PTT_OFF));
}

std::optional<int> RigClient::getStrength() {
    std::lock_guard<std::mutex> lk(mtx_);
    if (!ensureOpen()) return std::nullopt;
    value_t v; v.i = 0;
    if (!checkComm(rig_get_level(rig_, RIG_VFO_CURR, RIG_LEVEL_STRENGTH, &v))) return std::nullopt;
    return v.i;
}

std::optional<double> RigClient::getLevel(const std::string& name) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (!ensureOpen()) return std::nullopt;
    setting_t lvl = 0;
    if      (name == "SWR")     lvl = RIG_LEVEL_SWR;
    else if (name == "ALC")     lvl = RIG_LEVEL_ALC;
    else if (name == "RFPOWER") lvl = RIG_LEVEL_RFPOWER;
    else return std::nullopt;
    value_t v; v.f = 0.0f;
    if (!checkComm(rig_get_level(rig_, RIG_VFO_CURR, lvl, &v))) return std::nullopt;
    return v.f;
}

bool RigClient::setLevel(const std::string& name, double val) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (!ensureOpen()) return false;
    setting_t lvl = 0;
    if      (name == "RFPOWER") lvl = RIG_LEVEL_RFPOWER;
    else if (name == "ALC")     lvl = RIG_LEVEL_ALC;
    else return false;
    value_t v; v.f = static_cast<float>(val);
    return checkComm(rig_set_level(rig_, RIG_VFO_CURR, lvl, v));
}

RigStatus RigClient::poll(bool readSwr) {
    RigStatus st;
    auto f = getFrequency();
    if (!f) return st;
    st.freq_hz = *f;
    if (auto m = getMode())     { st.mode = m->first; st.passband = m->second; }
    if (auto p = getPtt())        st.ptt = *p;
    if (auto s = getStrength())   st.strength_db = *s;
    if (auto w = getLevel("RFPOWER")) st.power_w = *w * powerScale_.load();
    if (st.ptt && readSwr) { if (auto v = getLevel("SWR")) st.swr = *v; }
    st.ok = true;
    return st;
}

namespace {
int collectModel(const struct rig_caps* c, rig_ptr_t data) {
    auto* out = static_cast<std::vector<RigModel>*>(data);
    RigModel m;
    m.model  = c->rig_model;
    m.mfg    = c->mfg_name   ? c->mfg_name   : "";
    m.name   = c->model_name ? c->model_name : "";
    m.status = rig_strstatus(c->status);
    out->push_back(std::move(m));
    return 1;
}
}

std::vector<RigModel> RigClient::listModels() {
    hamlibInitOnce();
    std::vector<RigModel> out;
    rig_list_foreach(collectModel, &out);
    return out;
}

}
