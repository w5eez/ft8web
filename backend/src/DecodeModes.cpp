// SPDX-License-Identifier: GPL-3.0-or-later

#include "DecodeEngine.hpp"

namespace ft8web {

const std::vector<ModeSpec>& DecodeEngine::modes() {
    static const std::vector<ModeSpec> m = {
        {"FT8",    "-8", 15.0},
        {"FT4",    "-5", 7.5},
        {"FT2",    "--ft2", 3.75},
        {"WSPR",   "",   120.0},
    };
    return m;
}

const ModeSpec* DecodeEngine::findMode(const std::string& name) {
    for (const auto& m : modes()) if (m.name == name) return &m;
    return nullptr;
}

}
