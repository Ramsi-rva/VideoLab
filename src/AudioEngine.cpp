#include "AudioEngine.h"

#include <algorithm>
#include <cmath>

bool AudioEngine::start(const Project* p, const AudioBank* bank, double t0) {
    stop();
    p_ = p;
    bank_ = bank;

    WAVEFORMATEX wf{};
    wf.wFormatTag = WAVE_FORMAT_PCM;
    wf.nChannels = 2;
    wf.nSamplesPerSec = AudioSource::kRate;
    wf.wBitsPerSample = 16;
    wf.nBlockAlign = (WORD)(wf.nChannels * wf.wBitsPerSample / 8);
    wf.nAvgBytesPerSec = wf.nSamplesPerSec * wf.nBlockAlign;
    if (waveOutOpen(&hwo_, WAVE_MAPPER, &wf, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
        hwo_ = nullptr;
        return false;
    }
    for (int i = 0; i < kBufs; ++i) {
        data_[i].assign((size_t)kFrames * 2, 0);
        hdr_[i] = WAVEHDR{};
        hdr_[i].lpData = (LPSTR)data_[i].data();
        hdr_[i].dwBufferLength = (DWORD)(data_[i].size() * sizeof(int16_t));
        waveOutPrepareHeader(hwo_, &hdr_[i], sizeof(WAVEHDR));
    }
    restart(t0);
    return true;
}

void AudioEngine::restart(double t0) {
    if (!hwo_) return;
    waveOutReset(hwo_);   // todos los bufferes vuelven como "terminados" y la posicion pasa a 0
    for (int i = 0; i < kBufs; ++i) queued_[i] = false;
    next_ = 0;
    submitted_ = 0;
    t0Frame_ = std::llround(std::max(0.0, t0) * AudioSource::kRate);
    pump();
}

void AudioEngine::stop() {
    if (!hwo_) return;
    waveOutReset(hwo_);
    for (int i = 0; i < kBufs; ++i) waveOutUnprepareHeader(hwo_, &hdr_[i], sizeof(WAVEHDR));
    waveOutClose(hwo_);
    hwo_ = nullptr;
}

void AudioEngine::fill(int i) {
    mix_.resize((size_t)kFrames * 2);
    mixAudio(*p_, *bank_, t0Frame_ + submitted_, kFrames, mix_.data());
    float pl = 0, pr = 0;
    int16_t* d = data_[i].data();
    for (int k = 0; k < kFrames; ++k) {
        float l = mix_[(size_t)k * 2], r = mix_[(size_t)k * 2 + 1];
        pl = std::max(pl, std::fabs(l));
        pr = std::max(pr, std::fabs(r));
        d[k * 2] = (int16_t)std::lround(l * 32767.0f);
        d[k * 2 + 1] = (int16_t)std::lround(r * 32767.0f);
    }
    peakL_[i] = pl;
    peakR_[i] = pr;
    bufStart_[i] = submitted_;
    submitted_ += kFrames;
}

void AudioEngine::pump() {
    if (!hwo_ || !p_ || !bank_) return;
    for (int n = 0; n < kBufs; ++n) {
        int i = next_;
        if (queued_[i] && !(hdr_[i].dwFlags & WHDR_DONE)) break;   // sigue sonando: los demas tambien
        fill(i);
        if (waveOutWrite(hwo_, &hdr_[i], sizeof(WAVEHDR)) != MMSYSERR_NOERROR) { queued_[i] = false; break; }
        queued_[i] = true;
        next_ = (i + 1) % kBufs;
    }
}

double AudioEngine::position() const {
    if (!hwo_) return (double)t0Frame_ / AudioSource::kRate;
    MMTIME mt{};
    mt.wType = TIME_SAMPLES;
    if (waveOutGetPosition(hwo_, &mt, sizeof(mt)) != MMSYSERR_NOERROR || mt.wType != TIME_SAMPLES)
        return (double)t0Frame_ / AudioSource::kRate;
    return (double)(t0Frame_ + (int64_t)mt.u.sample) / AudioSource::kRate;
}

void AudioEngine::levels(float& l, float& r) const {
    l = r = 0.0f;
    if (!hwo_) return;
    MMTIME mt{};
    mt.wType = TIME_SAMPLES;
    if (waveOutGetPosition(hwo_, &mt, sizeof(mt)) != MMSYSERR_NOERROR || mt.wType != TIME_SAMPLES) return;
    int64_t pos = (int64_t)mt.u.sample;
    for (int i = 0; i < kBufs; ++i)
        if (queued_[i] && pos >= bufStart_[i] && pos < bufStart_[i] + kFrames) { l = peakL_[i]; r = peakR_[i]; return; }
}
