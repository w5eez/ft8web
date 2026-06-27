// SPDX-License-Identifier: GPL-3.0-or-later

#include "Waterfall.hpp"

#include <chrono>
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace ft8web {

Waterfall::Waterfall(AudioEngine& audio) : audio_(audio) {
    buildView(vNorm_, 2048, 0.0, 3000.0);
    buildView(vWspr_, 16384, 1400.0, 1600.0);
}

Waterfall::~Waterfall() {
    freeView(vNorm_);
    freeView(vWspr_);
}

void Waterfall::buildView(View& v, int fftSize, double startHz, double stopHz) {
    v.fft = fftSize;
    const double bh = static_cast<double>(AudioEngine::kSampleRate) / fftSize;
    v.startBin = static_cast<int>(std::floor(startHz / bh));
    v.nBins    = std::min(fftSize / 2 - v.startBin,
                          static_cast<int>(std::ceil((stopHz - startHz) / bh)));
    v.rel.resize(static_cast<size_t>(v.nBins));
    v.scratch.resize(static_cast<size_t>(v.nBins));
    v.window.resize(fftSize);
    for (int i = 0; i < fftSize; ++i)
        v.window[i] = 0.5f - 0.5f * std::cos(2.0f * static_cast<float>(M_PI) * i / (fftSize - 1));
    v.in   = fftwf_alloc_real(fftSize);
    v.out  = fftwf_alloc_complex(fftSize / 2 + 1);
    v.plan = fftwf_plan_dft_r2c_1d(fftSize, v.in, v.out, FFTW_ESTIMATE);
}

void Waterfall::freeView(View& v) {
    if (v.plan) fftwf_destroy_plan(v.plan);
    if (v.in)   fftwf_free(v.in);
    if (v.out)  fftwf_free(v.out);
    v.plan = nullptr; v.in = nullptr; v.out = nullptr;
}

void Waterfall::setWsprView(bool on) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (on == wspr_) return;
    wspr_ = on;
    std::lock_guard<std::mutex> hlk(histMtx_);
    hist_.clear();
}

int Waterfall::bins() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return active().nBins;
}
double Waterfall::binHz() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return static_cast<double>(AudioEngine::kSampleRate) / active().fft;
}
double Waterfall::startHz() const {
    std::lock_guard<std::mutex> lk(mtx_);
    const auto& v = active();
    return v.startBin * (static_cast<double>(AudioEngine::kSampleRate) / v.fft);
}

std::vector<uint8_t> Waterfall::frame() {
    std::lock_guard<std::mutex> lk(mtx_);
    View& v = wspr_ ? vWspr_ : vNorm_;
    std::vector<uint8_t> outv(static_cast<size_t>(v.nBins), 0);

    auto s = audio_.latest(static_cast<size_t>(v.fft));
    if (s.empty()) return outv;
    const int n = static_cast<int>(s.size());

    for (int i = 0; i < v.fft; ++i) {
        const float x = (i < n) ? (s[static_cast<size_t>(i)] / 32768.0f) : 0.0f;
        v.in[i] = x * v.window[i];
    }
    fftwf_execute(v.plan);

    const bool freshBaseline = static_cast<int>(v.baseline.size()) != v.nBins;
    if (freshBaseline) v.baseline.assign(static_cast<size_t>(v.nBins), 0.0f);

    constexpr float kFollowDown = 0.10f;
    constexpr float kFollowUp   = 0.004f;
    const float norm = v.fft / 2.0f;

    std::vector<float>& rel = v.rel;
    for (int k = 0; k < v.nBins; ++k) {
        const int b = v.startBin + k;
        const float re = v.out[b][0], im = v.out[b][1];
        const float mag = std::sqrt(re * re + im * im) / norm;
        const float db  = 20.0f * std::log10(mag + 1e-9f);
        float& base = v.baseline[static_cast<size_t>(k)];
        if (freshBaseline) base = db;
        else base += (db < base ? kFollowDown : kFollowUp) * (db - base);
        rel[static_cast<size_t>(k)] = db - base;
    }

    float noiseRel = 0.0f;
    {
        std::vector<float>& tmp = v.scratch;
        std::copy(rel.begin(), rel.end(), tmp.begin());
        auto mid = tmp.begin() + tmp.size() / 2;
        std::nth_element(tmp.begin(), mid, tmp.end());
        noiseRel = *mid;
    }
    constexpr float kNoiseMargin = 1.5f;
    constexpr float kSpanDb      = 24.0f;
    const float gain = 255.0f / kSpanDb;
    for (int k = 0; k < v.nBins; ++k) {
        float t = (rel[static_cast<size_t>(k)] - noiseRel - kNoiseMargin) * gain;
        if (t < 0.0f)   t = 0.0f;
        if (t > 255.0f) t = 255.0f;
        outv[static_cast<size_t>(k)] = static_cast<uint8_t>(t);
    }

    {
        const long long now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        std::lock_guard<std::mutex> hlk(histMtx_);
        hist_.push_back({now_ms, outv});
        while (hist_.size() > kHistRows) hist_.pop_front();
    }

    return outv;
}

std::vector<Waterfall::Row> Waterfall::history() const {
    std::lock_guard<std::mutex> hlk(histMtx_);
    return {hist_.begin(), hist_.end()};
}

}
