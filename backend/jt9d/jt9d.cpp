// SPDX-License-Identifier: GPL-3.0-or-later
//
// jt9d -- persistent jt9 driver.
//
// Keeps a single `jt9 -s <key>` process alive so its callsign hash table
// survives across decode periods (compound/hashed calls resolve over time,
// like the WSJT-X GUI). Feeds jt9 one period of audio at a time through Qt
// shared memory and proxies its decode lines back on stdout.
//
// Protocol on stdin, one request per line:
//   <nmode> <ntrperiod> <nutc> <nfqso> <ndepth> <nqsoprogress> <napwid> <mycall> <mygrid> <hiscall> <hisgrid> <wavpath>
// "-" stands in for an empty call/grid. nqsoprogress (0..5) and hiscall/hisgrid
// drive a-priori decoding of the weak in-QSO replies addressed to us, matching
// the WSJT-X GUI. On completion jt9d emits the decode lines verbatim followed
// by a single "<DecodeFinished>" line.

#include "commons.h"

#include <QSharedMemory>
#include <QString>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

#include <unistd.h>
#include <sys/wait.h>

namespace {

constexpr size_t kD2Len = sizeof(static_cast<dec_data*>(nullptr)->d2) / sizeof(short);
constexpr size_t kFt8Window = 50u * 3456u;

void setField(char* dst, int len, const std::string& s) {
    const std::string v = (s == "-") ? "" : s;
    for (int i = 0; i < len; ++i) dst[i] = i < static_cast<int>(v.size()) ? v[i] : ' ';
}

size_t readWav(const std::string& path, short* out, size_t maxn) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return 0;
    char riff[12];
    f.read(riff, 12);
    if (!f || std::memcmp(riff, "RIFF", 4) != 0 || std::memcmp(riff + 8, "WAVE", 4) != 0)
        return 0;
    while (f) {
        char id[4];
        uint32_t sz = 0;
        f.read(id, 4);
        f.read(reinterpret_cast<char*>(&sz), 4);
        if (!f) break;
        if (std::memcmp(id, "data", 4) == 0) {
            size_t n = sz / 2;
            if (n > maxn) n = maxn;
            f.read(reinterpret_cast<char*>(out), static_cast<std::streamsize>(n * 2));
            return static_cast<size_t>(f.gcount() / 2);
        }
        f.seekg(sz + (sz & 1), std::ios::cur);
    }
    return 0;
}

void fillParams(dec_data* dec, int nmode, int ntrperiod, int nutc, int nfqso, int ndepth,
                int nQSOProgress, int napwid, size_t nsamples,
                const std::string& mycall, const std::string& mygrid,
                const std::string& hiscall, const std::string& hisgrid) {
    auto& p = dec->params;
    // nzhsym = half-symbol spectra count, derived from the audio length the
    // same way jt9.f90's wav reader does. FT8 (mode 8) gets it overridden to
    // 50 inside jt9a's early-pass block, so this value only matters for the
    // shorter FT4/FT2 frames -- where a too-large value wrecks the SNR.
    const int nblk = static_cast<int>(nsamples / 3456);
    const int nzhsym = std::max(1, (nblk * 3456 - 2048) / 3456);
    p.nutc        = nutc;
    p.ndiskdat    = true;
    p.ntrperiod   = ntrperiod;
    p.nQSOProgress = nQSOProgress;
    p.nfqso       = nfqso;
    p.newdat      = true;
    p.npts8       = 74736;
    p.nfa         = 200;
    p.nfSplit     = 2700;
    p.nfb         = 4000;
    p.ntol        = 20;
    p.kin         = 64800;
    p.nzhsym      = nzhsym;
    p.nsubmode    = 0;
    p.nagain      = false;
    p.ndepth      = ndepth;
    p.lft8apon    = true;
    p.lapcqonly   = false;
    p.ljt65apon   = true;
    p.napwid      = napwid;
    p.ntxmode     = 65;
    p.nmode       = nmode;
    p.nclearave   = false;
    p.dttol       = 3.0f;
    p.n2pass      = 2;
    p.nranera     = 6;
    p.naggressive = 0;
    p.nrobust     = false;
    p.nexp_decode = 0;
    p.lmultift8   = false;
    setField(p.datetime, 20, "2013-Apr-16 15:13");
    setField(p.mycall, 12, mycall);
    setField(p.mygrid, 6, mygrid);
    setField(p.hiscall, 12, hiscall);
    setField(p.hisgrid, 6, hisgrid);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: jt9d <shmkey> <jt9path> <workdir>\n");
        return 2;
    }
    const std::string key = argv[1], jt9 = argv[2], work = argv[3];

    QSharedMemory shmem;
    shmem.setKey(QString::fromStdString(key));
    if (!shmem.create(sizeof(dec_data))) {
        std::fprintf(stderr, "jt9d: shmem create failed: %s\n",
                     shmem.errorString().toUtf8().constData());
        return 1;
    }
    auto* dec = static_cast<dec_data*>(shmem.data());
    std::memset(dec, 0, sizeof(dec_data));

    int outpipe[2];
    if (pipe(outpipe) != 0) return 1;
    const pid_t pid = fork();
    if (pid == 0) {
        dup2(outpipe[1], STDOUT_FILENO);
        close(outpipe[0]);
        close(outpipe[1]);
        execl(jt9.c_str(), "jt9", "-s", key.c_str(), "-a", work.c_str(),
              "-t", work.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }
    close(outpipe[1]);
    FILE* jout = fdopen(outpipe[0], "r");
    if (!jout) return 1;

    char req[2048];
    while (std::fgets(req, sizeof req, stdin)) {
        int nmode = 8, ntr = 15, nutc = 0, nfqso = 1500, ndepth = 2, nqso = 0, napwid = 75;
        char mycall[32], mygrid[32], hiscall[32], hisgrid[32], wav[1536];
        if (std::sscanf(req, "%d %d %d %d %d %d %d %31s %31s %31s %31s %1535s",
                        &nmode, &ntr, &nutc, &nfqso, &ndepth, &nqso, &napwid,
                        mycall, mygrid, hiscall, hisgrid, wav) != 12) {
            std::fputs("<DecodeFinished>\n", stdout);
            std::fflush(stdout);
            continue;
        }

        const size_t n = readWav(wav, dec->d2, kD2Len);
        // jt9.f90's wav reader caps the decode window per mode; match it so
        // FT4/FT2 SNR and decodes agree with the per-slot wav path.
        size_t window;
        if      (nmode == 5)  window = 21u * 3456u;  // FT4 = 72576
        else if (nmode == 52) window = 21u * 1728u;  // FT2 = 36288
        else                  window = kFt8Window;   // FT8 et al.
        const size_t keep = std::min(n, window);
        for (size_t i = keep; i < kFt8Window && i < kD2Len; ++i) dec->d2[i] = 0;
        fillParams(dec, nmode, ntr, nutc, nfqso, ndepth, nqso, napwid, keep,
                   mycall, mygrid, hiscall, hisgrid);

        shmem.lock();
        dec->ipc[1] = 1;  // Fortran ipc(2): tell jt9 to decode
        shmem.unlock();

        char line[1024];
        while (std::fgets(line, sizeof line, jout)) {
            if (std::strncmp(line, "<DecodeFinished>", 16) == 0) break;
            std::fputs(line, stdout);
        }

        shmem.lock();
        dec->ipc[2] = 1;  // Fortran ipc(3): acknowledge, release jt9 to loop
        shmem.unlock();

        std::fputs("<DecodeFinished>\n", stdout);
        std::fflush(stdout);
    }

    shmem.lock();
    dec->ipc[1] = 999;  // terminate jt9
    shmem.unlock();
    waitpid(pid, nullptr, 0);
    return 0;
}
