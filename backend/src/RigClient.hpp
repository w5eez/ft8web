// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>
#include <vector>
#include <utility>
#include <optional>
#include <atomic>
#include <mutex>

struct s_rig;

namespace ft8web {

struct RigStatus {
    long long   freq_hz     = 0;
    std::string mode;
    int         passband    = 0;
    bool        ptt         = false;
    int         strength_db = 0;
    double      power_w     = 0.0;
    double      swr         = 0.0;
    double      alc         = 0.0;
    bool        ok          = false;
};

struct RigModel {
    int         model = 0;
    std::string mfg;
    std::string name;
    std::string status;
};

class RigClient {
public:
    RigClient() = default;
    ~RigClient();
    RigClient(const RigClient&) = delete;
    RigClient& operator=(const RigClient&) = delete;

    void setRadio(int model, const std::string& device, int baud, double powerMaxW);

    std::optional<long long>                   getFrequency();
    bool                                       setFrequency(long long hz);
    std::optional<std::pair<std::string,int>>  getMode();
    bool                                       setMode(const std::string& mode, int passband = 0);
    std::optional<bool>                        getPtt();
    bool                                       setPtt(bool on);
    std::optional<int>                         getStrength();
    std::optional<double>                      getLevel(const std::string& name);
    bool                                       setLevel(const std::string& name, double v);

    double powerScale() const { return powerScale_.load(); }

    RigStatus poll(bool readSwr = true);
    bool      connected() const { return open_.load(); }

    static std::vector<RigModel> listModels();

private:
    bool ensureOpen();
    void closeRig();
    bool checkComm(int err);

    s_rig*      rig_ = nullptr;
    bool        wantOpen_ = false;
    int         model_ = 0;
    int         baud_  = 38400;
    std::string device_;
    long long   lastOpenMs_ = 0;
    std::mutex  mtx_;
    std::atomic<bool>   open_{false};
    std::atomic<double> powerScale_{100.0};
};

}
