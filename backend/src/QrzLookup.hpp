// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>
#include <map>
#include <mutex>

namespace ft8web {

struct CallInfo {
    bool ok = false;
    std::string error;
    std::string call, fname, name, addr1, addr2, state, zip, country, grid, image;
};

class QrzLookup {
public:
    void configure(const std::string& user, const std::string& pass);
    bool enabled() const;
    CallInfo lookup(const std::string& call);

private:

    static std::string login(const std::string& user, const std::string& pass, std::string& err);
    static std::string fetch(const std::string& url);
    static std::string xmlField(const std::string& xml, const std::string& tag);

    mutable std::mutex mtx_;
    std::string user_, pass_, sessionKey_;
    std::map<std::string, CallInfo> cache_;
};

}
