// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <cstdlib>
#include <stdexcept>
#include <string>

namespace ft8web {

inline std::string homeDir() {
    const char* h = std::getenv("HOME");
    if (!h || !*h) throw std::runtime_error("HOME is not set; run ft8web as a normal user");
    return h;
}

inline std::string ft8webRoot(const std::string& rel = "") {
    return homeDir() + "/ft8web" + (rel.empty() ? "" : "/" + rel);
}

inline std::string ft8webLogs(const std::string& rel = "") {
    return ft8webRoot("logs") + (rel.empty() ? "" : "/" + rel);
}

}
