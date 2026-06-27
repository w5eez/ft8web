// SPDX-License-Identifier: GPL-3.0-or-later

#include "FreqPlan.hpp"
#include "freq_plan_data.hpp"

#include <algorithm>
#include <set>
#include <cctype>

namespace ft8web {

namespace {
struct BandRange { long long lo, hi; const char* name; };
const BandRange kBands[] = {
    {135700,    137800,    "2200m"}, {472000,    479000,    "630m"},
    {1800000,   2000000,   "160m"},  {3500000,   4000000,   "80m"},
    {5250000,   5450000,   "60m"},   {7000000,   7300000,   "40m"},
    {10100000,  10150000,  "30m"},   {14000000,  14350000,  "20m"},
    {18068000,  18168000,  "17m"},   {21000000,  21450000,  "15m"},
    {24890000,  24990000,  "12m"},   {28000000,  29700000,  "10m"},
    {50000000,  54000000,  "6m"},    {70000000,  70500000,  "4m"},
    {144000000, 148000000, "2m"},    {222000000, 225000000, "1.25m"},
    {420000000, 450000000, "70cm"},
};
}

std::string FreqPlan::bandFor(long long hz) {
    for (const auto& b : kBands)
        if (hz >= b.lo && hz <= b.hi) return b.name;
    return "?";
}

FreqPlan::FreqPlan() {
    for (const auto& e : kDefaultFrequencies)
        entries_.push_back({e.hz, e.mode, e.region, bandFor(e.hz)});
}

std::vector<std::string> FreqPlan::modes() const {
    std::set<std::string> s;
    for (const auto& e : entries_) s.insert(e.mode);
    return {s.begin(), s.end()};
}

std::vector<PlanEntry> FreqPlan::forMode(const std::string& mode, const std::string& region) const {
    std::vector<PlanEntry> out;
    for (const auto& e : entries_) {
        if (e.mode != mode) continue;
        if (region != "ALL" && e.region != "ALL" && e.region != region) continue;
        out.push_back(e);
    }
    std::sort(out.begin(), out.end(), [](const PlanEntry& a, const PlanEntry& b) { return a.hz < b.hz; });
    return out;
}

std::optional<long long> FreqPlan::frequencyFor(const std::string& mode, const std::string& band,
                                                const std::string& region) const {

    std::optional<long long> worldwide, any;
    for (const auto& e : entries_) {
        if (e.mode != mode || e.band != band) continue;
        if (e.region == region) return e.hz;
        if (e.region == "ALL" && !worldwide) worldwide = e.hz;
        if (!any) any = e.hz;
    }
    return worldwide ? worldwide : any;
}

std::string FreqPlan::regionForGrid(const std::string& grid) {
    if (grid.size() < 2) return "ALL";
    char A = static_cast<char>(std::toupper(static_cast<unsigned char>(grid[0])));
    char B = static_cast<char>(std::toupper(static_cast<unsigned char>(grid[1])));
    if (A < 'A' || A > 'R' || B < 'A' || B > 'R') return "ALL";
    double lon = (A - 'A') * 20.0 - 180.0;
    double lat = (B - 'A') * 10.0 - 90.0;
    if (grid.size() >= 4 && std::isdigit(static_cast<unsigned char>(grid[2]))
                         && std::isdigit(static_cast<unsigned char>(grid[3]))) {
        lon += (grid[2] - '0') * 2.0 + 1.0;
        lat += (grid[3] - '0') * 1.0 + 0.5;
    } else {
        lon += 10.0; lat += 5.0;
    }
    if (lon <= -30.0) return "R2";
    if (lon <  60.0)  return "R1";
    return lat >= 43.0 ? "R1" : "R3";
}

}
