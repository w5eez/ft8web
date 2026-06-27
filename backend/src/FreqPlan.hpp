// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>
#include <vector>
#include <optional>
#include <cstdint>

namespace ft8web {

struct PlanEntry {
    long long   hz;
    std::string mode;
    std::string region;
    std::string band;
};

class FreqPlan {
public:
    FreqPlan();

    const std::vector<PlanEntry>& all() const { return entries_; }
    std::vector<std::string>      modes() const;

    std::vector<PlanEntry> forMode(const std::string& mode,
                                   const std::string& region = "ALL") const;

    std::optional<long long> frequencyFor(const std::string& mode, const std::string& band,
                                          const std::string& region = "ALL") const;

    static std::string bandFor(long long hz);

    static std::string regionForGrid(const std::string& grid);

private:
    std::vector<PlanEntry> entries_;
};

}
