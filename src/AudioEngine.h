// Reproduccion de audio en tiempo real con waveOut (WinMM). Se usa desde el hilo de la interfaz:
// pump() rellena los bufferes libres desde el temporizador. El reloj de audio manda sobre el de video.
#pragma once
#include "Util.h"

#include <mmsystem.h>

#include <cstdint>
#include <vector>

#include "AudioMix.h"
#include "Project.h"

class AudioEngine {
public:
    ~AudioEngine() { stop(); }

    bool start(const Project* p, const AudioBank* bank, double t0);  // false si no hay dispositivo
    void restart(double t0);       // salta a otro instante (cabezal movido o edicion)
    void stop();
    void pump();                   // llamar cada ~15 ms mientras suena
    bool running() const { return hwo_ != nullptr; }
    double position() const;       // tiempo de la linea de tiempo que se esta oyendo (s)
    void levels(float& l, float& r) const;   // pico (0..1) del bloque que suena ahora

private:
    static constexpr int kBufs = 6, kFrames = 2048;

    void fill(int i);

    HWAVEOUT hwo_ = nullptr;
    WAVEHDR  hdr_[kBufs] = {};
    std::vector<int16_t> data_[kBufs];
    std::vector<float>   mix_;
    bool     queued_[kBufs] = {};
    float    peakL_[kBufs] = {}, peakR_[kBufs] = {};
    int64_t  bufStart_[kBufs] = {};   // muestras reproducidas (desde el reinicio) cuando empieza cada bufer
    int      next_ = 0;
    int64_t  submitted_ = 0;          // muestras enviadas desde el ultimo reinicio
    int64_t  t0Frame_ = 0;            // muestra de la linea de tiempo en la que empezo
    const Project*   p_ = nullptr;
    const AudioBank* bank_ = nullptr;
};
