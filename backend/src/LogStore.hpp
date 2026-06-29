// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>
#include <vector>
#include <map>
#include <set>
#include <mutex>
#include <optional>

namespace ft8web {

struct LogInfo {
    std::string file;
    std::string name;
    std::string park;
    bool        active = false;
    long long   created = 0;
    int         qsos = 0;
    int         potaUploaded = 0;
    bool        archived = false;
};

using QsoFields = std::map<std::string, std::string>;

class LogStore {
public:
    explicit LogStore(std::string dir);

    void setStationCallsign(const std::string& call);

    std::vector<LogInfo>     list();

    std::optional<std::string> parkForFile(const std::string& file);
    std::optional<LogInfo>   create(const std::string& name, const std::string& park, bool active);
    bool                     remove(const std::string& file);
    bool                     setMeta(const std::string& file, const std::string* name,
                                     const std::string* park, const bool* active,
                                     const bool* archived = nullptr);

    int  qsoCounter() const;
    void bumpCounter();
    void resetCounter();

    std::vector<QsoFields>   qsos(const std::string& file);
    bool                     addQso(const std::string& file, QsoFields q);
    int                      addQsoToActive(QsoFields q);
    QsoFields                stampForUpload(QsoFields q) const;
    QsoFields                stampForFile(const std::string& file, QsoFields q) const;
    bool                     updateQso(const std::string& file, size_t idx, const QsoFields& q);
    bool                     deleteQso(const std::string& file, size_t idx);

    std::string              rawPath(const std::string& file) const;
    static bool              validFile(const std::string& file);
    static std::string       recordToAdif(const QsoFields& q);
    static std::vector<QsoFields> parseAdifRecords(const std::string& text);
    static void              normalizeQsoMode(QsoFields& q);

    std::vector<QsoFields>   potaPending(const std::string& file);
    std::string              potaUploadName(const std::string& file);

    int                      markPotaUploaded(const std::string& file,
                                              const std::set<std::string>& keys,
                                              const std::string& stamp);

    static std::string       qsoKey(const QsoFields& q);

    static std::string       buildAdif(const std::vector<QsoFields>& records);

    static std::string       buildCabrillo(const std::vector<QsoFields>& records,
                                           const std::string& callsign);

private:
    struct Meta { std::string name, park; bool active = false; bool archived = false; long long created = 0; };

    void loadMeta();
    void saveMeta() const;
    std::string adifPath(const std::string& file) const;
    std::string cabrilloPath(const std::string& file) const;
    void writeCabrillo(const std::string& file);
    bool writeAll(const std::string& file, const std::vector<QsoFields>& records);
    QsoFields stamped(QsoFields q, const Meta& m) const;
    static void normalizeMode(QsoFields& q);

    std::string                 dir_;
    mutable std::mutex          mtx_;
    std::map<std::string, Meta> meta_;
    std::string                 stationCall_;
    int                         counter_ = 0;
};

}
