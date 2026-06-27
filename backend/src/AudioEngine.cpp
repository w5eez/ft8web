// SPDX-License-Identifier: GPL-3.0-or-later

#include "AudioEngine.hpp"

#include <alsa/asoundlib.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <fstream>
#include <iterator>
#include <filesystem>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace ft8web {

namespace {

long long nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string readFirstLine(const std::string& path) {
    std::ifstream f(path);
    std::string s;
    if (f) std::getline(f, s);
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
    return s;
}

std::string cardUsbId(int idx) {
    return readFirstLine("/proc/asound/card" + std::to_string(idx) + "/usbid");
}

std::string cardUsbPort(int idx) {
    std::error_code ec;
    std::filesystem::path link = "/sys/class/sound/card" + std::to_string(idx) + "/device";
    auto real = std::filesystem::canonical(link, ec);
    if (ec) return {};

    std::string name = real.parent_path().filename().string();
    if (name.find('-') == std::string::npos || name.find(':') != std::string::npos)
        name = real.filename().string();
    if (name.find('-') == std::string::npos || name.find(':') != std::string::npos) return {};
    return name;
}

struct CapCard {
    int         index = -1;
    std::string alsaId;
    std::string name;
    std::string vidpid;
    std::string port;
};

std::vector<CapCard> enumCaptureCards() {
    std::vector<CapCard> out;
    int card = -1;
    while (snd_card_next(&card) == 0 && card >= 0) {
        char ctlName[32];
        std::snprintf(ctlName, sizeof ctlName, "hw:%d", card);
        snd_ctl_t* ctl = nullptr;
        if (snd_ctl_open(&ctl, ctlName, 0) < 0) continue;
        snd_ctl_card_info_t* cinfo;
        snd_ctl_card_info_alloca(&cinfo);
        if (snd_ctl_card_info(ctl, cinfo) < 0) { snd_ctl_close(ctl); continue; }
        const char* cardId = snd_ctl_card_info_get_id(cinfo);
        const char* cardNm = snd_ctl_card_info_get_name(cinfo);

        std::string pcmName;
        bool hasCapture = false;
        int dev = -1;
        while (snd_ctl_pcm_next_device(ctl, &dev) == 0 && dev >= 0) {
            snd_pcm_info_t* info;
            snd_pcm_info_alloca(&info);
            snd_pcm_info_set_device(info, dev);
            snd_pcm_info_set_subdevice(info, 0);
            snd_pcm_info_set_stream(info, SND_PCM_STREAM_CAPTURE);
            if (snd_ctl_pcm_info(ctl, info) < 0) continue;
            hasCapture = true;
            pcmName = snd_pcm_info_get_name(info);
            break;
        }
        snd_ctl_close(ctl);
        if (!hasCapture) continue;

        CapCard c;
        c.index  = card;
        c.alsaId = cardId;
        c.name   = std::string(cardNm) + " — " + pcmName;
        c.vidpid = cardUsbId(card);
        c.port   = cardUsbPort(card);
        out.push_back(std::move(c));
    }
    return out;
}

}

AudioEngine::~AudioEngine() { stop(); }

std::vector<AudioDevice> AudioEngine::listCaptureDevices() {
    std::vector<AudioDevice> out;
    for (const auto& c : enumCaptureCards()) {
        const bool usb = !c.vidpid.empty() && !c.port.empty();
        AudioDevice d;

        d.id   = usb ? ("usb:" + c.vidpid + "@" + c.port)
                     : ("plughw:CARD=" + c.alsaId + ",DEV=0");
        d.name = c.name + " [" + (usb ? (c.vidpid + " @ " + c.port) : c.alsaId) + "]";
        out.push_back(std::move(d));
    }
    return out;
}

std::string AudioEngine::resolveDevice(const std::string& stored) {
    if (stored.rfind("usb:", 0) != 0) return stored;

    std::string body = stored.substr(4);
    std::string vidpid, port;
    if (auto at = body.find('@'); at != std::string::npos) {
        vidpid = body.substr(0, at);
        port   = body.substr(at + 1);
    } else {
        vidpid = body;
    }

    int bestScore = 0;
    std::string bestId;
    for (const auto& c : enumCaptureCards()) {
        int score = 0;
        if (!port.empty()   && c.port == port)     score += 2;
        if (!vidpid.empty() && c.vidpid == vidpid) score += 1;
        if (score > bestScore) { bestScore = score; bestId = c.alsaId; }
    }
    if (bestScore <= 0 || bestId.empty()) return {};
    return "plughw:CARD=" + bestId + ",DEV=0";
}

bool AudioEngine::silent() const {
    if (!running_.load() || txActive_.load()) return false;
    const long long ref = std::max(lastLoudMs_.load(), captureStartMs_.load());
    return ref != 0 && (nowMs() - ref) > 3000;
}

void AudioEngine::setTransmitting(bool on) {
    txActive_.store(on);

    if (!on) lastLoudMs_.store(nowMs());
}

bool AudioEngine::start(const std::string& device) {
    std::lock_guard<std::mutex> ctl(ctlMtx_);
    stopLocked();
    const std::string pcm = resolveDevice(device);
    {

        std::lock_guard<std::mutex> lk(mtx_);
        device_  = device;
        pcm_     = pcm;
        cap_     = static_cast<size_t>(kSampleRate) * 150;
        ring_.assign(cap_, 0);
        written_ = 0;
    }
    if (pcm.empty()) {
        { std::lock_guard<std::mutex> le(errMtx_); lastError_ = "audio device not found: " + device; }
        running_.store(false);
        std::fprintf(stderr, "[audio] no card matches selector '%s'\n", device.c_str());
        return false;
    }
    { std::lock_guard<std::mutex> le(errMtx_); lastError_.clear(); }
    lastLoudMs_.store(0);
    captureStartMs_.store(nowMs());
    openMixer();
    running_.store(true);
    thread_  = std::thread(&AudioEngine::captureLoop, this);
    return true;
}

void AudioEngine::stop() {
    std::lock_guard<std::mutex> ctl(ctlMtx_);
    stopLocked();
}

void AudioEngine::stopLocked() {
    running_.store(false);
    if (thread_.joinable()) thread_.join();
    closeMixer();
}

bool AudioEngine::playWavFile(const std::string& device, const std::string& path,
                              float amplitude, double maxSeconds) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::vector<char> raw((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (raw.size() < 44) return false;

    auto u16 = [&](size_t o) { return static_cast<uint16_t>(static_cast<uint8_t>(raw[o]) |
                                                            (static_cast<uint8_t>(raw[o + 1]) << 8)); };
    auto u32 = [&](size_t o) { return static_cast<uint32_t>(static_cast<uint8_t>(raw[o]) |
                                                            (static_cast<uint8_t>(raw[o + 1]) << 8) |
                                                            (static_cast<uint8_t>(raw[o + 2]) << 16) |
                                                            (static_cast<uint8_t>(raw[o + 3]) << 24)); };
    const unsigned channels = u16(22);
    const unsigned rate     = u32(24);
    const unsigned bits     = u16(34);
    if (bits != 16 || channels < 1 || rate < 8000) return false;

    size_t dataOff = 0, dataLen = 0;
    for (size_t o = 12; o + 8 <= raw.size();) {
        const uint32_t len = u32(o + 4);
        if (std::memcmp(&raw[o], "data", 4) == 0) {
            dataOff = o + 8;
            dataLen = std::min<size_t>(len, raw.size() - dataOff);
            break;
        }
        o += 8 + len + (len & 1);
    }
    if (!dataLen) return false;

    const int16_t* samples = reinterpret_cast<const int16_t*>(&raw[dataOff]);
    size_t total = dataLen / 2 / channels;
    if (maxSeconds > 0) total = std::min(total, static_cast<size_t>(maxSeconds * rate));

    snd_pcm_t* pcm = nullptr;
    if (snd_pcm_open(&pcm, device.c_str(), SND_PCM_STREAM_PLAYBACK, 0) < 0) return false;
    snd_pcm_hw_params_t* hw;
    snd_pcm_hw_params_alloca(&hw);
    snd_pcm_hw_params_any(pcm, hw);
    snd_pcm_hw_params_set_access(pcm, hw, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(pcm, hw, SND_PCM_FORMAT_S16_LE);
    snd_pcm_hw_params_set_channels(pcm, hw, 1);
    unsigned int r = rate;
    snd_pcm_hw_params_set_rate_near(pcm, hw, &r, nullptr);
    if (snd_pcm_hw_params(pcm, hw) < 0) { snd_pcm_close(pcm); return false; }

    playRun_.store(true);
    const size_t N = 1024;
    std::vector<int16_t> buf(N);
    size_t pos = 0;
    bool ok = true;
    const auto playStart = std::chrono::steady_clock::now();
    while (playRun_.load() && pos < total) {
        const size_t n = std::min(N, total - pos);
        for (size_t i = 0; i < n; ++i) {
            float v = samples[(pos + i) * channels] * amplitude;
            if (v > 32767.f)  v = 32767.f;
            if (v < -32768.f) v = -32768.f;
            buf[i] = static_cast<int16_t>(v);
        }
        snd_pcm_sframes_t w = snd_pcm_writei(pcm, buf.data(), n);
        if (w < 0 && snd_pcm_recover(pcm, static_cast<int>(w), 1) < 0) { ok = false; break; }
        pos += n;
    }
    if (playRun_.load() && ok) {

        const double audioSec = static_cast<double>(total) / rate;
        while (playRun_.load() &&
               std::chrono::duration<double>(std::chrono::steady_clock::now() - playStart).count()
                   < audioSec + 0.05)
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        snd_pcm_drain(pcm);
    } else {
        snd_pcm_drop(pcm);
    }
    snd_pcm_close(pcm);
    playRun_.store(false);
    return ok;
}

void AudioEngine::captureLoop() {
    std::string pcmName;
    { std::lock_guard<std::mutex> lk(mtx_); pcmName = pcm_; }
    snd_pcm_t* pcm = nullptr;
    if (snd_pcm_open(&pcm, pcmName.c_str(), SND_PCM_STREAM_CAPTURE, 0) < 0) {
        { std::lock_guard<std::mutex> le(errMtx_);
          lastError_ = "could not open audio device " + pcmName + " (in use or unplugged)"; }
        std::fprintf(stderr, "[audio] open failed: %s\n", pcmName.c_str());
        running_.store(false);
        return;
    }

    snd_pcm_hw_params_t* hw;
    snd_pcm_hw_params_alloca(&hw);
    snd_pcm_hw_params_any(pcm, hw);
    snd_pcm_hw_params_set_access(pcm, hw, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(pcm, hw, SND_PCM_FORMAT_S16_LE);

    unsigned int channels = 2;
    snd_pcm_hw_params_set_channels_near(pcm, hw, &channels);
    if (channels < 1) channels = 1;
    unsigned int rate = kSampleRate;
    snd_pcm_hw_params_set_rate_near(pcm, hw, &rate, nullptr);
    snd_pcm_uframes_t period = 2048;
    snd_pcm_hw_params_set_period_size_near(pcm, hw, &period, nullptr);
    if (snd_pcm_hw_params(pcm, hw) < 0) {
        { std::lock_guard<std::mutex> le(errMtx_);
          lastError_ = "audio device rejected 12 kHz S16 capture"; }
        snd_pcm_close(pcm);
        running_.store(false);
        return;
    }

    if (rate != static_cast<unsigned int>(kSampleRate))
        std::fprintf(stderr, "[audio] WARNING: capture rate %u Hz != %d Hz — decode timing/"
                             "frequency will be wrong. Use a 'plughw:' device.\n", rate, kSampleRate);
    snd_pcm_prepare(pcm);

    std::vector<int16_t> buf(static_cast<size_t>(period) * channels);
    std::vector<int16_t> mono(period);
    while (running_.load()) {
        snd_pcm_sframes_t n = snd_pcm_readi(pcm, buf.data(), period);
        if (n == -EAGAIN) continue;
        if (n < 0) {
            if (snd_pcm_recover(pcm, static_cast<int>(n), 1) < 0) break;
            xruns_.fetch_add(1, std::memory_order_relaxed);
            continue;
        }
        if (n == 0) continue;

        auto capNow = std::chrono::system_clock::now();
        {
            snd_pcm_sframes_t qd = 0;
            if (snd_pcm_delay(pcm, &qd) == 0 && qd > 0)
                capNow -= std::chrono::duration_cast<std::chrono::system_clock::duration>(
                    std::chrono::duration<double>(static_cast<double>(qd) / kSampleRate));
        }

        const unsigned sel = (channels >= 2 && chSel_.load()) ? 1u : 0u;
        if (channels == 1) {
            std::copy(buf.data(), buf.data() + n, mono.data());
        } else {
            for (snd_pcm_sframes_t i = 0; i < n; ++i)
                mono[static_cast<size_t>(i)] = buf[static_cast<size_t>(i) * channels + sel];
        }

        double sumsq = 0.0;
        int peak = 0;
        for (snd_pcm_sframes_t i = 0; i < n; ++i) {
            int s = mono[static_cast<size_t>(i)];
            sumsq += static_cast<double>(s) * s;
            int a = s < 0 ? -s : s;
            if (a > peak) peak = a;
        }
        const float rms = static_cast<float>(std::sqrt(sumsq / n) / 32768.0);
        level_.store(rms);

        if (rms > 0.0005f) lastLoudMs_.store(nowMs());
        if (peak >= 32700) {
            lastClipMs_.store(std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
        }

        {
            std::lock_guard<std::mutex> lk(mtx_);

            const size_t cnt = static_cast<size_t>(n);
            const size_t pos = static_cast<size_t>(written_ % cap_);
            const size_t first = std::min(cnt, cap_ - pos);
            std::copy(mono.data(), mono.data() + first, ring_.data() + pos);
            if (cnt > first) std::copy(mono.data() + first, mono.data() + cnt, ring_.data());
            written_ += static_cast<uint64_t>(n);
            lastTime_ = capNow;
        }
    }
    snd_pcm_close(pcm);
}

bool AudioEngine::clipping() const {
    long long last = lastClipMs_.load();
    if (last == 0) return false;
    long long now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return (now - last) < 1500;
}

std::string AudioEngine::cardOf(const std::string& dev) {
    auto p = dev.find("CARD=");
    if (p == std::string::npos) return {};
    p += 5;
    auto e = dev.find_first_of(",]", p);
    return dev.substr(p, e == std::string::npos ? std::string::npos : e - p);
}

void AudioEngine::openMixer() {
    const std::string card = cardOf(pcm_);
    std::lock_guard<std::mutex> lk(mixMtx_);
    if (mixer_) { snd_mixer_close(static_cast<snd_mixer_t*>(mixer_)); mixer_ = nullptr; }
    capElem_ = nullptr;
    hasCapVol_ = false;
    if (card.empty()) return;

    int idx = snd_card_get_index(card.c_str());
    if (idx < 0) return;

    snd_mixer_t* mx = nullptr;
    if (snd_mixer_open(&mx, 0) < 0) return;
    const std::string ctl = "hw:" + std::to_string(idx);
    if (snd_mixer_attach(mx, ctl.c_str()) < 0 ||
        snd_mixer_selem_register(mx, nullptr, nullptr) < 0 ||
        snd_mixer_load(mx) < 0) {
        snd_mixer_close(mx);
        return;
    }
    snd_mixer_elem_t* cap = nullptr;
    for (snd_mixer_elem_t* e = snd_mixer_first_elem(mx); e; e = snd_mixer_elem_next(e)) {
        if (snd_mixer_selem_has_capture_volume(e)) { cap = e; break; }
    }
    mixer_     = mx;
    capElem_   = cap;
    hasCapVol_ = (cap != nullptr);

    std::string agc = "amixer -c " + std::to_string(idx) +
                      " -q sset 'Auto Gain Control' off >/dev/null 2>&1";
    std::system(agc.c_str());
}

void AudioEngine::closeMixer() {
    std::lock_guard<std::mutex> lk(mixMtx_);
    if (mixer_) { snd_mixer_close(static_cast<snd_mixer_t*>(mixer_)); mixer_ = nullptr; }
    capElem_ = nullptr;
    hasCapVol_ = false;
}

int AudioEngine::captureVolume() const {
    std::lock_guard<std::mutex> lk(mixMtx_);
    if (!hasCapVol_ || !capElem_) return -1;
    auto* el = static_cast<snd_mixer_elem_t*>(capElem_);
    snd_mixer_handle_events(static_cast<snd_mixer_t*>(mixer_));
    long mn = 0, mx = 0, v = 0;
    snd_mixer_selem_get_capture_volume_range(el, &mn, &mx);
    snd_mixer_selem_get_capture_volume(el, SND_MIXER_SCHN_FRONT_LEFT, &v);
    if (mx <= mn) return 0;
    return static_cast<int>(std::lround((v - mn) * 100.0 / (mx - mn)));
}

void AudioEngine::setCaptureVolume(int pct) {
    std::lock_guard<std::mutex> lk(mixMtx_);
    if (!hasCapVol_ || !capElem_) return;
    auto* el = static_cast<snd_mixer_elem_t*>(capElem_);
    long mn = 0, mx = 0;
    snd_mixer_selem_get_capture_volume_range(el, &mn, &mx);
    pct = std::max(0, std::min(100, pct));
    long v = mn + static_cast<long>(std::lround(pct / 100.0 * (mx - mn)));
    snd_mixer_selem_set_capture_volume_all(el, v);
}

std::vector<int16_t> AudioEngine::latest(size_t n) const {
    std::lock_guard<std::mutex> lk(mtx_);
    if (written_ == 0) return {};
    n = static_cast<size_t>(std::min<uint64_t>(n, std::min<uint64_t>(written_, cap_)));
    std::vector<int16_t> out(n);
    const size_t pos = static_cast<size_t>((written_ - n) % cap_);
    const size_t first = std::min(n, cap_ - pos);
    std::copy(ring_.data() + pos, ring_.data() + pos + first, out.data());
    if (n > first) std::copy(ring_.data(), ring_.data() + (n - first), out.data() + first);
    return out;
}

std::vector<int16_t> AudioEngine::extractPeriod(std::chrono::system_clock::time_point start,
                                                double seconds) const {
    std::lock_guard<std::mutex> lk(mtx_);
    if (written_ == 0) return {};
    double behind = std::chrono::duration<double>(lastTime_ - start).count();
    long long startIdx = static_cast<long long>(written_) -
                         static_cast<long long>(behind * kSampleRate);
    long long count    = static_cast<long long>(seconds * kSampleRate);
    long long endIdx   = startIdx + count;
    if (startIdx < static_cast<long long>(written_ - cap_))  return {};
    if (startIdx < 0)                                        return {};

    if (endIdx > static_cast<long long>(written_)) {
        endIdx = static_cast<long long>(written_);
        count  = endIdx - startIdx;
    }
    if (count <= 0)                                          return {};

    const size_t cnt = static_cast<size_t>(count);
    std::vector<int16_t> out(cnt);
    const size_t pos = static_cast<size_t>(static_cast<uint64_t>(startIdx) % cap_);
    const size_t first = std::min(cnt, cap_ - pos);
    std::copy(ring_.data() + pos, ring_.data() + pos + first, out.data());
    if (cnt > first) std::copy(ring_.data(), ring_.data() + (cnt - first), out.data() + first);
    return out;
}

}
