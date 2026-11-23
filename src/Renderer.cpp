#include "Renderer.h"

#include <algorithm>
#include <cmath>
#include <cstring>

bool Canvas::create(int W, int H) {
    destroy();
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = W;
    bi.bmiHeader.biHeight = -H; // de arriba hacia abajo
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    HDC scr = GetDC(nullptr);
    dc = CreateCompatibleDC(scr);
    bmp = CreateDIBSection(scr, &bi, DIB_RGB_COLORS, (void**)&bits, nullptr, 0);
    ReleaseDC(nullptr, scr);
    if (!dc || !bmp || !bits) { destroy(); return false; }
    old = SelectObject(dc, bmp);
    w = W;
    h = H;
    return true;
}

void Canvas::destroy() {
    if (dc && old) SelectObject(dc, old);
    if (bmp) DeleteObject(bmp);
    if (dc) DeleteDC(dc);
    dc = nullptr; bmp = nullptr; old = nullptr; bits = nullptr;
    w = h = 0;
}

static inline uint8_t clamp8(int v) { return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v)); }

MediaSource* Renderer::source(const Project& p, int idx) {
    auto it = cache_.find(idx);
    if (it != cache_.end()) return it->second.get();
    if (failed_.count(idx) || idx < 0 || idx >= (int)p.media.size()) return nullptr;

    auto ms = std::make_unique<MediaSource>();
    std::string err;
    if (!ms->open(p.media[idx].path, err)) { failed_.insert(idx); return nullptr; }
    MediaSource* raw = ms.get();
    cache_[idx] = std::move(ms);
    return raw;
}

// ---- Correccion de color primaria: tablas por canal (0=R, 1=G, 2=B) ----
static bool gradeNeutral(const Clip& c) {
    return c.brightness == 0 && c.contrast == 0 && c.temp == 0 && c.tint == 0 &&
           c.lift.neutral() && c.gamma.neutral() && c.gain.neutral();
}

static void buildLuts(const Clip& c, uint8_t lut[3][256]) {
    const double PI = 3.14159265358979323846;
    auto wheelRGB = [&](const Wheel& w, double o[3]) {
        double m = std::min(1.0, std::sqrt(w.x * w.x + w.y * w.y));
        double th = std::atan2(w.y, w.x);
        o[0] = m * std::cos(th);                    // rojo a 0 grados
        o[1] = m * std::cos(th - 2.0 * PI / 3.0);   // verde a 120
        o[2] = m * std::cos(th - 4.0 * PI / 3.0);   // azul a 240
    };
    double dl[3], dm[3], dg[3];
    wheelRGB(c.lift, dl);
    wheelRGB(c.gamma, dm);
    wheelRGB(c.gain, dg);

    double temp[3] = { 1.0 + c.temp * 0.004, 1.0 - c.tint * 0.003, 1.0 - c.temp * 0.004 };
    const double cf = (100.0 + c.contrast) / 100.0;
    const double bright = c.brightness * 0.005;

    for (int k = 0; k < 3; ++k) {
        double lift = c.lift.master * 0.25 + dl[k] * 0.15;
        double gain = (1.0 + c.gain.master * 0.5 + dg[k] * 0.3) * temp[k];
        double gam = std::clamp(1.0 + c.gamma.master * 0.6 + dm[k] * 0.3, 0.25, 3.0);
        double e = 1.0 / gam;
        for (int i = 0; i < 256; ++i) {
            double x = i / 255.0;
            double v = x * gain + lift * (1.0 - x);   // lift actua en sombras, gain en luces
            v = std::clamp(v, 0.0, 1.0);
            v = std::pow(v, e);                       // gamma actua en medios tonos
            v = (v - 0.5) * cf + 0.5 + bright;
            lut[k][i] = clamp8((int)(v * 255.0 + 0.5));
        }
    }
}

void Renderer::adjust(uint8_t* px, int w, int h, const Clip& c) {
    const size_t n = (size_t)w * h;

    if (!gradeNeutral(c)) {
        uint8_t lut[3][256];
        buildLuts(c, lut);
        for (size_t k = 0; k < n; ++k) { // pixel BGRA
            uint8_t* p = px + k * 4;
            p[0] = lut[2][p[0]];
            p[1] = lut[1][p[1]];
            p[2] = lut[0][p[2]];
        }
    }

    if (c.saturation != 0) {
        int s = (100 + c.saturation) * 256 / 100;
        for (size_t k = 0; k < n; ++k) {
            uint8_t* p = px + k * 4;
            int y = (29 * p[0] + 150 * p[1] + 77 * p[2]) >> 8;
            p[0] = clamp8(y + (((int)p[0] - y) * s >> 8));
            p[1] = clamp8(y + (((int)p[1] - y) * s >> 8));
            p[2] = clamp8(y + (((int)p[2] - y) * s >> 8));
        }
    }

    switch (c.filter) {
    case Filter::None:
        break;
    case Filter::Gray:
        for (size_t k = 0; k < n; ++k) {
            uint8_t* p = px + k * 4;
            uint8_t y = (uint8_t)((29 * p[0] + 150 * p[1] + 77 * p[2]) >> 8);
            p[0] = p[1] = p[2] = y;
        }
        break;
    case Filter::Invert:
        for (size_t k = 0; k < n; ++k) {
            uint8_t* p = px + k * 4;
            p[0] = 255 - p[0]; p[1] = 255 - p[1]; p[2] = 255 - p[2];
        }
        break;
    case Filter::Sobel: {
        gray_.resize(n);
        for (size_t k = 0; k < n; ++k) {
            const uint8_t* p = px + k * 4;
            gray_[k] = (uint8_t)((29 * p[0] + 150 * p[1] + 77 * p[2]) >> 8);
        }
        auto G = [&](int x, int y) { return (int)gray_[(size_t)y * w + x]; };
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                uint8_t* p = px + ((size_t)y * w + x) * 4;
                if (x == 0 || y == 0 || x == w - 1 || y == h - 1) { p[0] = p[1] = p[2] = 0; continue; }
                int gx = -G(x-1,y-1) - 2*G(x-1,y) - G(x-1,y+1) + G(x+1,y-1) + 2*G(x+1,y) + G(x+1,y+1);
                int gy = -G(x-1,y-1) - 2*G(x,y-1) - G(x+1,y-1) + G(x-1,y+1) + 2*G(x,y+1) + G(x+1,y+1);
                uint8_t m = clamp8((int)std::sqrt((double)(gx * gx + gy * gy)));
                p[0] = p[1] = p[2] = m;
            }
        }
        break;
    }
    }
}

void Renderer::render(const Project& p, double t, Canvas& c) {
    if (!c.bits) return;
    GdiFlush(); // terminar dibujos GDI pendientes del frame anterior antes de tocar los pixeles
    std::memset(c.bits, 0, (size_t)c.w * c.h * 4);

    double local = 0.0;
    int ci = p.clipAt(t, local);
    if (ci >= 0) {
        const Clip& cl = p.clips[ci];
        MediaSource* src = source(p, cl.media);
        if (src && src->width() > 0 && src->height() > 0) {
            // Tamano en pantalla: ajustar al lienzo y aplicar el zoom del clip
            double fit = std::min((double)c.w / src->width(), (double)c.h / src->height());
            double z = std::clamp(cl.zoom, 10, 400) / 100.0;
            int tw = std::max(2, (int)(src->width() * fit * z)) & ~1;
            int th = std::max(2, (int)(src->height() * fit * z)) & ~1;
            // Decodificar a lo sumo al doble del lienzo
            double sd = std::min(1.0, std::min(2.0 * c.w / tw, 2.0 * c.h / th));
            int dw = std::max(2, (int)(tw * sd)) & ~1;
            int dh = std::max(2, (int)(th * sd)) & ~1;
            tmp_.resize((size_t)dw * dh * 4);

            double srcT = cl.srcIn + local * cl.speed;
            if (src->getFrame(srcT, dw, dh, tmp_.data())) {
                adjust(tmp_.data(), dw, dh, cl);

                // Fundido a negro: al inicio de este clip y al final del anterior
                double f = 1.0;
                if (cl.trans == Trans::FadeBlack && cl.transDur > 0.0 && local < cl.transDur)
                    f = std::min(f, local / cl.transDur);
                if (ci + 1 < (int)p.clips.size() && p.clips[ci + 1].trans == Trans::FadeBlack) {
                    double td = p.clips[ci + 1].transDur;
                    double rem = cl.duration() - local;
                    if (td > 0.0 && rem < td) f = std::min(f, rem / td);
                }
                int f256 = std::clamp((int)(f * 256.0 + 0.5), 0, 256);

                // Colocar en el lienzo con desplazamiento (posicion X/Y)
                int x0 = (c.w - tw) / 2 + cl.posX * c.w / 100;
                int y0 = (c.h - th) / 2 + cl.posY * c.h / 100;
                int xs = std::max(0, x0), xe = std::min(c.w, x0 + tw);
                int ys = std::max(0, y0), ye = std::min(c.h, y0 + th);
                if (xe > xs && ye > ys) {
                    const bool direct = (dw == tw && dh == th && f256 >= 256);
                    xmap_.resize((size_t)(xe - xs));
                    for (int x = xs; x < xe; ++x)
                        xmap_[x - xs] = std::min(dw - 1, (int)((int64_t)(x - x0) * dw / tw));
                    for (int y = ys; y < ye; ++y) {
                        int sy = std::min(dh - 1, (int)((int64_t)(y - y0) * dh / th));
                        const uint8_t* sp = tmp_.data() + (size_t)sy * dw * 4;
                        uint8_t* d = c.bits + ((size_t)y * c.w + xs) * 4;
                        if (direct) {
                            std::memcpy(d, sp + (size_t)(xs - x0) * 4, (size_t)(xe - xs) * 4);
                        } else {
                            for (int x = 0; x < xe - xs; ++x) {
                                const uint8_t* q = sp + (size_t)xmap_[x] * 4;
                                d[x * 4 + 0] = (uint8_t)((q[0] * f256) >> 8);
                                d[x * 4 + 1] = (uint8_t)((q[1] * f256) >> 8);
                                d[x * 4 + 2] = (uint8_t)((q[2] * f256) >> 8);
                                d[x * 4 + 3] = 255;
                            }
                        }
                    }
                }
            }
        }
    }

    drawTexts(p, t, c);
    GdiFlush();
}

void Renderer::drawTexts(const Project& p, double t, Canvas& c) {
    for (const auto& ti : p.texts) {
        if (t < ti.start || t >= ti.start + ti.dur || ti.text.empty()) continue;

        int fh = std::max(8, (int)(c.h * ti.size / 100.0));
        HFONT font = CreateFontW(-fh, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                                 ANTIALIASED_QUALITY, 0, L"Segoe UI");
        HGDIOBJ oldFont = SelectObject(c.dc, font);
        SetBkMode(c.dc, TRANSPARENT);

        const UINT fl = DT_WORDBREAK | DT_CENTER | DT_NOPREFIX;
        RECT r{ 0, 0, (LONG)(c.w * 0.9), 0 };
        DrawTextW(c.dc, ti.text.c_str(), -1, &r, fl | DT_CALCRECT);
        int tw = r.right - r.left, th = r.bottom - r.top;
        int x = (int)(ti.x * c.w - tw / 2.0);
        int y = (int)(ti.y * c.h - th / 2.0);
        x = std::clamp(x, 0, std::max(0, c.w - tw));
        y = std::clamp(y, 0, std::max(0, c.h - th));
        OffsetRect(&r, x, y);

        if (ti.box) { // oscurecer el area detras del texto
            GdiFlush();
            int pad = fh / 3;
            int x0 = std::max(0, (int)r.left - pad), x1 = std::min(c.w, (int)r.right + pad);
            int y0 = std::max(0, (int)r.top - pad), y1 = std::min(c.h, (int)r.bottom + pad);
            for (int yy = y0; yy < y1; ++yy) {
                uint8_t* row = c.bits + ((size_t)yy * c.w + x0) * 4;
                for (int i = 0; i < (x1 - x0) * 4; ++i) row[i] >>= 1;
            }
        }

        int sh = std::max(1, fh / 16);
        RECT s = r;
        OffsetRect(&s, sh, sh);
        SetTextColor(c.dc, RGB(0, 0, 0));
        DrawTextW(c.dc, ti.text.c_str(), -1, &s, fl);
        SetTextColor(c.dc, RGB((ti.rgb >> 16) & 255, (ti.rgb >> 8) & 255, ti.rgb & 255));
        DrawTextW(c.dc, ti.text.c_str(), -1, &r, fl);

        SelectObject(c.dc, oldFont);
        DeleteObject(font);
    }
}

bool makeThumbnail(MediaSource& ms, double t, Canvas& out) {
    if (!out.bits || ms.width() <= 0 || ms.height() <= 0) return false;
    GdiFlush();
    std::memset(out.bits, 0, (size_t)out.w * out.h * 4);
    double s = std::min((double)out.w / ms.width(), (double)out.h / ms.height());
    int dw = std::max(2, (int)(ms.width() * s));
    int dh = std::max(2, (int)(ms.height() * s));
    dw = std::min(dw, out.w);
    dh = std::min(dh, out.h);
    std::vector<uint8_t> buf((size_t)dw * dh * 4);
    if (!ms.getFrame(t, dw, dh, buf.data())) return false;
    int ox = (out.w - dw) / 2, oy = (out.h - dh) / 2;
    for (int y = 0; y < dh; ++y)
        std::memcpy(out.bits + ((size_t)(oy + y) * out.w + ox) * 4, buf.data() + (size_t)y * dw * 4, (size_t)dw * 4);
    return true;
}
