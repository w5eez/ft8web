// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "AudioEngine.hpp"

#include <deque>
#include <vector>
#include <mutex>
#include <cstdint>
#include <fftw3.h>

namespace ft8web {

class Waterfall {
public:
    explicit Waterfall(AudioEngine& audio);
    ~Waterfall();
    Waterfall(const Waterfall&) = delete;
    Waterfall& operator=(const Waterfall&) = delete;

    void setWsprView(bool on);

    struct Row { long long t_ms; std::vector<uint8_t> bins; };
    std::vector<Row> history() const;

    std::vector<uint8_t> frame();
    int    bins() const;
    double binHz() const;
    double startHz() const;

private:
    struct View {
        int                fft = 0;
        int                startBin = 0;
        int                nBins = 0;
        std::vector<float> window;
        std::vector<float> baseline;
        std::vector<float> rel;
        std::vector<float> scratch;
        float*             in = nullptr;
        fftwf_complex*     out = nullptr;
        fftwf_plan         plan = nullptr;
    };
    void buildView(View& v, int fftSize, double startHz, double stopHz);
    void freeView(View& v);
    const View& active() const { return wspr_ ? vWspr_ : vNorm_; }

    AudioEngine&       audio_;
    mutable std::mutex mtx_;
    View               vNorm_, vWspr_;
    bool               wspr_ = false;
    mutable std::mutex histMtx_;
    std::deque<Row>    hist_;
    static constexpr size_t kHistRows = 512;
};

}
