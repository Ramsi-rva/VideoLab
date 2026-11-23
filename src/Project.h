// MODELO: datos del proyecto de edicion (medios, clips, textos, correccion de color).
#pragma once
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

enum class Filter { None = 0, Gray, Sobel, Invert };
enum class Trans { None = 0, FadeBlack };

struct MediaInfo {
    std::string  path;      // UTF-8
    std::wstring name;
    double duration = 0.0;  // segundos
    int    width = 0, height = 0;
    double fps = 30.0;
    bool   hasVideo = true;   // tiene flujo de video (no solo portada)
    bool   hasAudio = false;  // tiene flujo de audio
    double audioDur = 0.0;    // duracion del audio decodificado (s)
};

// Rueda de color: (x, y) es la posicion del punto dentro del circulo unitario (y hacia arriba);
// master es el control de luminosidad de la rueda (-1..1).
struct Wheel {
    double x = 0, y = 0, master = 0;
    bool neutral() const { return x == 0 && y == 0 && master == 0; }
};

struct Clip {
    int    media = 0;                    // indice en Project::media
    double srcIn = 0.0, srcOut = 0.0;    // recorte dentro del archivo original (s)
    double speed = 1.0;                  // 0.25x .. 4x

    // Transformar
    int zoom = 100;                      // % (10..400)
    int posX = 0, posY = 0;              // -100..100 (% del cuadro)

    // Color primario
    int brightness = 0, contrast = 0, saturation = 0; // -100..100
    int temp = 0, tint = 0;                           // -100..100
    Wheel lift, gamma, gain;

    Filter filter = Filter::None;
    Trans  trans = Trans::None;          // transicion al INICIO del clip
    double transDur = 0.5;

    double duration() const { return (srcOut - srcIn) / speed; } // duracion en la linea de tiempo
};

// Clip de audio: va en las pistas A1 / A2 y se mezcla en la reproduccion y en la exportacion.
struct AudioClip {
    int    media = 0;                    // indice en Project::media
    double srcIn = 0.0, srcOut = 0.0;    // recorte dentro del archivo (s)
    double start = 0.0;                  // posicion en la linea de tiempo (s)
    int    track = 0;                    // 0 = A1, 1 = A2
    int    volume = 100;                 // 0..200 %
    double fadeIn = 0.0, fadeOut = 0.0;  // fundidos (s)
    bool   mute = false;
    double duration() const { return srcOut - srcIn; }
    double end() const { return start + duration(); }
};

struct TextItem {
    std::wstring text = L"Texto";
    double start = 0.0, dur = 3.0;       // en la linea de tiempo (s)
    double x = 0.5, y = 0.85;            // centro, fraccion del cuadro (0..1)
    int    size = 8;                     // % de la altura del video
    uint32_t rgb = 0xFFFFFF;             // 0xRRGGBB
    bool   box = false;                  // fondo oscuro detras del texto
};

struct Project {
    std::vector<MediaInfo> media;
    std::vector<Clip>      clips;        // pista de video, contiguos
    std::vector<TextItem>  texts;        // pista de texto
    std::vector<AudioClip> audio;        // pistas de audio A1 / A2
    int    outW = 1280, outH = 720;
    double fps = 30.0;
    int    bitrateKbps = 0;              // 0 = automatico

    double videoDuration() const {
        double t = 0;
        for (const auto& c : clips) t += c.duration();
        return t;
    }
    double audioEnd() const {
        double t = 0;
        for (const auto& a : audio) t = std::max(t, a.end());
        return t;
    }
    // Duracion total: lo que dure lo mas largo entre video y audio.
    double duration() const { return std::max(videoDuration(), audioEnd()); }
    bool hasAudio() const {
        for (const auto& a : audio) if (!a.mute && a.volume > 0 && a.duration() > 0) return true;
        return false;
    }
    double clipStart(int i) const {
        double t = 0;
        for (int k = 0; k < i; ++k) t += clips[k].duration();
        return t;
    }
    // Clip que se muestra en el instante t; 'local' = segundos desde el inicio del clip.
    int clipAt(double t, double& local) const {
        double s = 0;
        for (size_t i = 0; i < clips.size(); ++i) {
            double d = clips[i].duration();
            if (t < s + d) { local = std::max(0.0, t - s); return (int)i; }
            s += d;
        }
        if (!clips.empty() && t <= s + 1e-6) {
            local = std::max(0.0, clips.back().duration() - 1e-6);
            return (int)clips.size() - 1;
        }
        return -1;
    }
};
