// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>
#include <atomic>
#include <thread>
#include <mutex>

namespace ft8web {

class GpsClient {
public:
    explicit GpsClient(std::string host = "127.0.0.1", int port = 2947);
    ~GpsClient();
    GpsClient(const GpsClient&) = delete;
    GpsClient& operator=(const GpsClient&) = delete;

    struct Fix {
        bool   valid = false;
        double lat = 0.0, lon = 0.0;
        long long age_s = -1;
    };
    Fix fix() const;
    std::string grid() const;

    static std::string latLonToGrid(double lat, double lon);
    static bool        gridToLatLon(const std::string& grid, double& lat, double& lon);

private:
    void loop();

    std::string host_;
    int         port_;

    mutable std::mutex mtx_;
    bool      valid_ = false;
    double    lat_ = 0.0, lon_ = 0.0;
    long long lastFixMs_ = 0;

    std::atomic<bool> run_{true};
    std::thread       thread_;
};

}
