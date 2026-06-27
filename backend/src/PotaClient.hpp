// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>
#include <vector>

namespace ft8web {

class PotaClient {
public:
    struct Park {
        std::string reference;
        std::string name;
        double      lat = 0.0, lon = 0.0;
        double      distKm = 0.0;
    };

    static std::vector<Park> parksInBox(double south, double west, double north, double east,
                                        double centerLat, double centerLon, size_t max);

    static bool downloadParksCsv(const std::string& path, std::string& err);
    static std::vector<Park> loadParksCsv(const std::string& path);
    static std::vector<Park> parksInBoxLocal(const std::vector<Park>& db,
                                             double south, double west, double north, double east,
                                             double centerLat, double centerLon, size_t max);

    struct Detail {
        bool        ok = false;
        std::string reference, name, grid, locationName, locationDesc, parktype, website;
        double      lat = 0.0, lon = 0.0;
        int         activations = 0, attempts = 0, qsos = 0;
    };
    static Detail parkDetail(const std::string& reference);

    struct SpotResult {
        bool        ok = false;
        std::string error;
    };

    static SpotResult spot(const std::string& activator, const std::string& reference,
                           const std::string& freqKhz, const std::string& mode,
                           const std::string& comments);

    struct UploadResult {
        bool        ok = false;
        int         status = 0;
        std::string error;
        std::string detail;
    };

    static UploadResult uploadAdif(const std::string& user, const std::string& pass,
                                   const std::string& adif);

    struct ActivationsResult {
        bool                     ok = false;
        std::vector<std::string> refs;
        std::string              error;
    };

    static ActivationsResult userActivations(const std::string& user, const std::string& pass);

private:
    static std::string httpGet(const std::string& url);
};

}
