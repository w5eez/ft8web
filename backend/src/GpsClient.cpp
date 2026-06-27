// SPDX-License-Identifier: GPL-3.0-or-later

#include "GpsClient.hpp"

#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <cmath>
#include <chrono>
#include <cstring>
#include <cerrno>
#include "crow/json.h"

namespace ft8web {

namespace {
long long nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}
constexpr long long kStaleMs = 5000;
}

GpsClient::GpsClient(std::string host, int port)
    : host_(std::move(host)), port_(port) {
    thread_ = std::thread(&GpsClient::loop, this);
}

GpsClient::~GpsClient() {
    run_.store(false);
    if (thread_.joinable()) thread_.join();
}

GpsClient::Fix GpsClient::fix() const {
    std::lock_guard<std::mutex> l(mtx_);
    Fix f;
    f.lat = lat_;
    f.lon = lon_;
    f.age_s = lastFixMs_ ? (nowMs() - lastFixMs_) / 1000 : -1;
    f.valid = valid_ && lastFixMs_ && (nowMs() - lastFixMs_ < kStaleMs);
    return f;
}

std::string GpsClient::grid() const {
    Fix f = fix();
    return f.valid ? latLonToGrid(f.lat, f.lon) : std::string{};
}

std::string GpsClient::latLonToGrid(double lat, double lon) {
    double lo = lon + 180.0, la = lat + 90.0;
    if (lo < 0) lo = 0; if (lo >= 360) lo = 359.99999;
    if (la < 0) la = 0; if (la >= 180) la = 179.99999;
    char g[7];
    g[0] = static_cast<char>('A' + static_cast<int>(lo / 20));
    g[1] = static_cast<char>('A' + static_cast<int>(la / 10));
    g[2] = static_cast<char>('0' + static_cast<int>(std::fmod(lo, 20) / 2));
    g[3] = static_cast<char>('0' + static_cast<int>(std::fmod(la, 10)));
    g[4] = static_cast<char>('a' + static_cast<int>(std::fmod(lo, 2) / (2.0 / 24)));
    g[5] = static_cast<char>('a' + static_cast<int>(std::fmod(la, 1) / (1.0 / 24)));
    g[6] = '\0';
    return std::string(g);
}

bool GpsClient::gridToLatLon(const std::string& grid, double& lat, double& lon) {
    std::string g;
    for (char c : grid) g += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (g.size() < 4 ||
        g[0] < 'A' || g[0] > 'R' || g[1] < 'A' || g[1] > 'R' ||
        !std::isdigit(static_cast<unsigned char>(g[2])) ||
        !std::isdigit(static_cast<unsigned char>(g[3]))) return false;
    lon = (g[0] - 'A') * 20.0 - 180.0 + (g[2] - '0') * 2.0;
    lat = (g[1] - 'A') * 10.0 -  90.0 + (g[3] - '0') * 1.0;
    if (g.size() >= 6 && g[4] >= 'A' && g[4] <= 'X' && g[5] >= 'A' && g[5] <= 'X') {
        lon += (g[4] - 'A') * (2.0 / 24.0) + 1.0 / 24.0;
        lat += (g[5] - 'A') * (1.0 / 24.0) + 0.5 / 24.0;
    } else {
        lon += 1.0;
        lat += 0.5;
    }
    return true;
}

void GpsClient::loop() {
    while (run_.load()) {
        struct addrinfo hints{};
        hints.ai_family   = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        struct addrinfo* res = nullptr;
        int fd = -1;
        if (::getaddrinfo(host_.c_str(), std::to_string(port_).c_str(), &hints, &res) == 0) {
            for (auto* p = res; p; p = p->ai_next) {
                fd = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
                if (fd < 0) continue;
                if (::connect(fd, p->ai_addr, p->ai_addrlen) == 0) break;
                ::close(fd);
                fd = -1;
            }
            ::freeaddrinfo(res);
        }
        if (fd < 0) {
            for (int i = 0; i < 20 && run_.load(); ++i) usleep(100000);
            continue;
        }

        const char* watch = "?WATCH={\"enable\":true,\"json\":true}\n";
        ::send(fd, watch, std::strlen(watch), MSG_NOSIGNAL);

        struct timeval tv{1, 0};
        ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);

        std::string buf;
        char tmp[4096];
        while (run_.load()) {
            ssize_t n = ::recv(fd, tmp, sizeof tmp, 0);
            if (n > 0) {
                buf.append(tmp, static_cast<size_t>(n));
                size_t nl;
                while ((nl = buf.find('\n')) != std::string::npos) {
                    std::string line = buf.substr(0, nl);
                    buf.erase(0, nl + 1);
                    while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
                        line.pop_back();

                    try {
                        auto j = crow::json::load(line);
                        if (!j || !j.has("class")) continue;
                        if (j["class"].t() != crow::json::type::String ||
                            std::string(j["class"].s()) != "TPV") continue;
                        const int mode = (j.has("mode") && j["mode"].t() == crow::json::type::Number)
                                         ? static_cast<int>(j["mode"].i()) : 0;
                        if (mode >= 2 &&
                            j.has("lat") && j["lat"].t() == crow::json::type::Number &&
                            j.has("lon") && j["lon"].t() == crow::json::type::Number) {
                            std::lock_guard<std::mutex> l(mtx_);
                            valid_     = true;
                            lat_       = j["lat"].d();
                            lon_       = j["lon"].d();
                            lastFixMs_ = nowMs();
                        }
                    } catch (const std::exception&) {   }
                }
                if (buf.size() > 65536) buf.clear();
            } else if (n == 0) {
                break;
            } else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
                break;
            }
        }
        ::close(fd);
        { std::lock_guard<std::mutex> l(mtx_); valid_ = false; }
        for (int i = 0; i < 10 && run_.load(); ++i) usleep(100000);
    }
}

}
