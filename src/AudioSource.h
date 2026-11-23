// Audio decodificado con FFmpeg a PCM 16 bits, estereo, 44100 Hz (todo el archivo en memoria).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

class AudioSource {
public:
    static constexpr int kRate = 44100;
    static constexpr int kPeakWin = 441;   // una muestra de pico cada 10 ms (100 por segundo)

    bool load(const std::string& utf8Path, std::string& err);

    int64_t        frames() const { return (int64_t)(pcm_.size() / 2); }
    double         duration() const { return (double)frames() / kRate; }
    const int16_t* samples() const { return pcm_.data(); }          // intercalado L R L R ...
    const std::vector<uint8_t>& peaks() const { return peaks_; }    // 0..255 por ventana de 10 ms

private:
    std::vector<int16_t> pcm_;
    std::vector<uint8_t> peaks_;
};

// Que flujos reales tiene un archivo (las portadas de un mp3 no cuentan como video).
struct StreamInfo {
    bool   hasVideo = false, hasAudio = false;
    double duration = 0.0;
};
bool probeStreams(const std::string& utf8Path, StreamInfo& info, std::string& err);
