// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace ft8web {

struct Decode;

struct QsoConfigSnapshot {
    std::string callsign, grid, opMode, fdClass, fdSection;
    bool autoCq   = false;
    bool homeEven = true;
    int  retries  = 5;
    int  maxCqs   = 0;
    bool cqSkip   = false;
    bool findClearFreqBeforeCq = false;
};

struct QsoHooks {
    std::function<bool(const std::string& msg, const std::string& mode, std::string& err)> txArm;
    std::function<void(const std::string& msg)> txUpdate;
    std::function<void()>                       txDisarm;
    std::function<bool()>                       txArmed;
    std::function<void(int hz)>                 setRxOffset;
    std::function<long long()>                  dialHz;
    std::function<int()>                        txOffsetHz;
    std::function<double()>                     txPowerW;
    std::function<std::string()>                radioMode;
    std::function<void(const std::map<std::string, std::string>&)> logQso;
    std::function<void(const std::string& json)> pushState;
};

class QsoEngine {
public:
    explicit QsoEngine(QsoHooks h);

    void applyConfig(const QsoConfigSnapshot& c);
    void onDecode(const Decode& d);
    void onTxDone();
    void tick();
    void clientsAlive(bool alive);
    void onModeChange(const std::string& mode);

    void callDecode(const Decode& d);
    void setDxCall(const std::string& call);
    void setDxGrid(const std::string& grid);
    void setReport(int report);
    void selectTx(int n);
    void abortQso();
    void refreshTxMessage();
    bool armRequest(std::string& err);

    bool qsoActive() const;
    bool qsoInProgress() const;
    bool effectiveEven() const;
    bool skipTxSlot(long long slot);
    std::string stateJson() const;
    void apContext(std::string& hisCall, std::string& hisGrid, int& qsoProgress) const;

    static std::string fmtRpt(int snr);
    static int  replyTx(int stage);
    struct Stage { int stage = 0; std::string rcvd; };
    static Stage classifyStage(const std::vector<std::string>& toks, bool fd);
    static bool decodeIsEvenUtc(const std::string& utc, double periodS, bool& evenOut);
    static long long epochFromUtc(const std::string& utc, long long nowMs);
    struct Sender { std::string call, grid; };
    static Sender parseSender(const std::string& message);
    static std::vector<std::string> stdMessages(const QsoConfigSnapshot& cfg,
                                                const std::string& dxCall, int report);
    static std::string wsprMessage(const std::string& callsign, const std::string& grid,
                                   double watts);

private:
    struct State {
        std::string dxCall, dxGrid;
        int         report = 0;
        int         selectedTx = 6;
        bool        complete = false;
        long long   completedEpoch = 0;
        int         attempts = 0;
        int         cqCount = 0;
        long long   cqRefSlot = -1;
        int         lastStage = -1;
        std::string lastUtc;
        long long   lastHeardMs = 0;
        double      lastPeriodS = 15.0;
        std::string rcvdReport;
        bool        logged = false;
        bool        txedThisQso = false;
        long long   startedEpoch = 0;

        bool homeEven = true;
        bool flipped  = false;
        bool flipTo   = true;

        bool clientsAlive = true;
        std::string lastEvent;
        long long   seq = 0;
        int rxOffset = 1500;
        std::string myGridLocked;
    };

    void onDecodeLocked(const Decode& d, std::vector<std::function<void()>>& fx);
    void abortLocked(std::vector<std::function<void()>>& fx);
    void idleAfterQsoLocked(std::vector<std::function<void()>>& fx);
    bool trackStageLocked(int stage, const std::string& utc,
                          std::vector<std::function<void()>>& fx);
    void resetProgressLocked(long long nowMs);
    void adoptOppositeSlotLocked(const Decode& d);
    void setRxOffsetLocked(int hz, std::vector<std::function<void()>>& fx);
    void logQsoLocked(std::vector<std::function<void()>>& fx);
    void syncTxLocked(std::vector<std::function<void()>>& fx);
    void pushLocked(std::vector<std::function<void()>>& fx);
    std::string stateJsonLocked() const;
    std::string stateJsonWithEvent(const std::string& event) const;
    std::vector<std::string> messagesLocked() const;
    long long nowMs() const;

    QsoHooks           hooks_;
    mutable std::mutex mtx_;
    QsoConfigSnapshot  cfg_;
    State              st_;
    std::string        mode_ = "FT8";

public:
    std::function<long long()> nowOverride;
};

}
