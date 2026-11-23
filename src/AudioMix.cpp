#include "AudioMix.h"

#include <algorithm>
#include <cmath>
#include <cstring>

void mixAudio(const Project& p, const AudioBank& bank, int64_t startFrame, int frames, float* out) {
    std::memset(out, 0, sizeof(float) * (size_t)frames * 2);
    const double R = AudioSource::kRate;
    const int64_t endFrame = startFrame + frames;

    for (const AudioClip& c : p.audio) {
        if (c.mute || c.volume <= 0 || c.duration() <= 0) continue;
        const AudioSource* s = bank.get(c.media);
        if (!s) continue;
        const int64_t cs = std::llround(c.start * R);
        const int64_t ce = cs + std::llround(c.duration() * R);
        const int64_t a = std::max(cs, startFrame), b = std::min(ce, endFrame);
        if (a >= b) continue;
        const int64_t srcBase = std::llround(c.srcIn * R) - cs;   // muestra de origen = f + srcBase
        const int64_t total = s->frames();
        const int16_t* pcm = s->samples();
        const float vol = c.volume / 100.0f;
        const double fi = c.fadeIn * R, fo = c.fadeOut * R;

        for (int64_t f = a; f < b; ++f) {
            const int64_t sp = f + srcBase;
            if (sp < 0 || sp >= total) continue;
            float g = vol;
            const double fromStart = (double)(f - cs), toEnd = (double)(ce - f);
            if (fi > 0 && fromStart < fi) g *= (float)(fromStart / fi);
            if (fo > 0 && toEnd < fo) g *= (float)(toEnd / fo);
            float* o = out + (size_t)(f - startFrame) * 2;
            o[0] += pcm[sp * 2] * (1.0f / 32768.0f) * g;
            o[1] += pcm[sp * 2 + 1] * (1.0f / 32768.0f) * g;
        }
    }
    for (int i = 0; i < frames * 2; ++i) out[i] = std::min(1.0f, std::max(-1.0f, out[i]));
}
