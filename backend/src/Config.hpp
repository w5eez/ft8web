// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <functional>

namespace ft8web {

class Config {
public:
    explicit Config(std::string path);

    std::string callsign() const;
    std::string grid() const;
    bool        autoGrid() const;
    bool        pskEnabled() const;

    struct Radio {
        std::string name      = "Radio";
        int         model     = 0;
        std::string serial;
        int         baud      = 38400;
        std::string device;
        double      powerMaxW = 100.0;
        std::string dataMode  = "PKTUSB";
        int         rxChannel = 0;
        int         rxFilterHz = 3000;
    };

    struct Values {
        std::string callsign, grid, opMode, fdClass, fdSection;
        bool pskEnabled = false;

        std::string lotwUser, lotwPass;
        std::string qrzApiKey;
        std::string qrzUser, qrzPass;
        std::string potaUser, potaPass;
        bool lotwUpload = false;
        bool qrzUpload  = false;

        bool txEven     = true;
        bool autoCq     = false;
        int  qsoRetries = 5;
        int  maxCqs     = 0;
        bool cqSkip     = false;
        bool findClearFreqBeforeCq = false;
        int  txLevel    = 50;
        bool   swrProtect = true;
        double swrMax     = 2.5;
        bool   autoGrid   = false;
        std::string units = "km";
        bool        showCalls = true;
        double      wfGain    = 1.0;
        std::string wfPalette = "Classic";
        std::vector<Radio> radios;
        int    activeRadio = 0;
        std::string radioMode   = "FT8";
        long long   radioDialHz = 0;
    };
    Values values() const;
    void   set(const Values& v);

    void   update(const std::function<void(Values&)>& mut);

private:
    void load();
    void save() const;

    std::string        path_;
    mutable std::mutex mtx_;
    Values             v_;
};

}
