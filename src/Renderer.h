// Compone un frame del proyecto (clip + color + transformar + filtros + fundidos + texto) en un lienzo.
#pragma once
#include "Util.h"

#include <map>
#include <memory>
#include <set>
#include <vector>

#include "MediaSource.h"
#include "Project.h"

// Lienzo BGRA (DIB section): los pixeles se escriben directo y el texto se dibuja con GDI encima.
struct Canvas {
    int w = 0, h = 0;
    HDC dc = nullptr;
    HBITMAP bmp = nullptr;
    HGDIOBJ old = nullptr;
    uint8_t* bits = nullptr;

    Canvas() = default;
    Canvas(const Canvas&) = delete;
    Canvas& operator=(const Canvas&) = delete;
    ~Canvas() { destroy(); }

    bool create(int W, int H);
    void destroy();
};

class Renderer {
public:
    void render(const Project& p, double t, Canvas& c);

private:
    MediaSource* source(const Project& p, int mediaIdx);
    void adjust(uint8_t* px, int w, int h, const Clip& c);
    void drawTexts(const Project& p, double t, Canvas& c);

    std::map<int, std::unique_ptr<MediaSource>> cache_;
    std::set<int> failed_;
    std::vector<uint8_t> tmp_, gray_;
    std::vector<int> xmap_;
};

// Miniatura aspect-correct (BGRA) de un instante del video, dentro de un lienzo dw x dh.
bool makeThumbnail(MediaSource& ms, double t, Canvas& out);
