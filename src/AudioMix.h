// Mezclador de audio: suma los clips de las pistas A1/A2 (volumen, fundidos, mute) a PCM float.
#pragma once
#include <memory>
#include <vector>

#include "AudioSource.h"
#include "Project.h"

struct AudioBank {
    std::vector<std::shared_ptr<AudioSource>> src;   // por indice de medio (nullptr si no tiene audio)
    const AudioSource* get(int media) const {
        return (media >= 0 && media < (int)src.size()) ? src[(size_t)media].get() : nullptr;
    }
};

// Escribe 'frames' muestras estereo intercaladas (float, -1..1) de la linea de tiempo
// a partir de la muestra 'startFrame' (44100 Hz).
void mixAudio(const Project& p, const AudioBank& bank, int64_t startFrame, int frames, float* out);
