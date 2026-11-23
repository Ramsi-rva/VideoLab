#include "Scopes.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "Theme.h"

static const double kPi = 3.14159265358979323846;

static inline void put(uint8_t* o, int W, int x, int y, int r, int g, int b) {
    uint8_t* p = o + ((size_t)y * W + x) * 4;
    p[0] = (uint8_t)std::min(255, std::max((int)p[0], b));
    p[1] = (uint8_t)std::min(255, std::max((int)p[1], g));
    p[2] = (uint8_t)std::min(255, std::max((int)p[2], r));
}

static void grid(Canvas& cv, bool horizontal, int levels) {
    // lineas de referencia (0..255 en 'levels' pasos)
    HFONT of = (HFONT)SelectObject(cv.dc, fSmall);
    SetBkMode(cv.dc, TRANSPARENT);
    SetTextColor(cv.dc, RGB(120, 120, 132));
    for (int i = 0; i <= levels; ++i) {
        if (horizontal) {
            int y = (cv.h - 1) - i * (cv.h - 1) / levels;
            lineTo(cv.dc, 0, y, cv.w, y, RGB(44, 44, 52));
            wchar_t b[8];
            wfmt(b, L"%d", i * 255 / levels);
            TextOutW(cv.dc, 3, std::min(cv.h - 14, std::max(0, y - 6)), b, (int)wcslen(b));
        } else {
            int x = i * (cv.w - 1) / levels;
            lineTo(cv.dc, x, 0, x, cv.h, RGB(44, 44, 52));
        }
    }
    SelectObject(cv.dc, of);
}

void renderScope(int mode, const uint8_t* src, int sw, int sh, Canvas& cv) {
    if (!cv.bits || !src || sw <= 0 || sh <= 0) return;
    GdiFlush();
    const int W = cv.w, H = cv.h;
    uint8_t* o = cv.bits;

    // fondo
    for (size_t i = 0; i < (size_t)W * H; ++i) {
        o[i * 4 + 0] = 16; o[i * 4 + 1] = 16; o[i * 4 + 2] = 18; o[i * 4 + 3] = 255;
    }

    static std::vector<uint32_t> acc;
    static float tbl[32];
    static bool tblInit = false;
    if (!tblInit) {
        for (int i = 0; i < 32; ++i) tbl[i] = 1.0f - std::exp(-i * 0.5f);
        tblInit = true;
    }
    auto level = [&](uint32_t n) { return n >= 31 ? 1.0f : tbl[n]; };

    if (mode == SC_WAVE) {
        acc.assign((size_t)W * H, 0);
        for (int y = 0; y < sh; ++y) {
            const uint8_t* row = src + (size_t)y * sw * 4;
            for (int x = 0; x < sw; ++x) {
                const uint8_t* p = row + (size_t)x * 4;
                int l = (29 * p[0] + 150 * p[1] + 77 * p[2]) >> 8;
                int col = x * W / sw, ry = (255 - l) * (H - 1) / 255;
                acc[(size_t)ry * W + col]++;
            }
        }
        grid(cv, true, 4);
        GdiFlush();
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                uint32_t n = acc[(size_t)y * W + x];
                if (!n) continue;
                float v = level(n);
                put(o, W, x, y, (int)(70 * v), (int)(255 * v), (int)(110 * v));
            }
    } else if (mode == SC_PARADE) {
        const int pw = W / 3;
        acc.assign((size_t)W * H, 0);
        for (int y = 0; y < sh; ++y) {
            const uint8_t* row = src + (size_t)y * sw * 4;
            for (int x = 0; x < sw; ++x) {
                const uint8_t* p = row + (size_t)x * 4;
                int col = x * pw / sw;
                acc[(size_t)((255 - p[2]) * (H - 1) / 255) * W + col]++;
                acc[(size_t)((255 - p[1]) * (H - 1) / 255) * W + pw + col]++;
                acc[(size_t)((255 - p[0]) * (H - 1) / 255) * W + 2 * pw + col]++;
            }
        }
        grid(cv, true, 4);
        GdiFlush();
        const int cols[3][3] = { { 255, 70, 70 }, { 70, 255, 90 }, { 80, 140, 255 } };
        for (int k = 0; k < 3; ++k)
            for (int y = 0; y < H; ++y)
                for (int x = 0; x < pw; ++x) {
                    uint32_t n = acc[(size_t)y * W + k * pw + x];
                    if (!n) continue;
                    float v = level(n);
                    put(o, W, k * pw + x, y, (int)(cols[k][0] * v), (int)(cols[k][1] * v), (int)(cols[k][2] * v));
                }
        lineTo(cv.dc, pw, 0, pw, H, RGB(60, 60, 68));
        lineTo(cv.dc, 2 * pw, 0, 2 * pw, H, RGB(60, 60, 68));
    } else if (mode == SC_HIST) {
        uint32_t hist[3][256] = {};
        for (size_t i = 0; i < (size_t)sw * sh; ++i) {
            const uint8_t* p = src + i * 4;
            hist[0][p[2]]++; hist[1][p[1]]++; hist[2][p[0]]++;
        }
        uint32_t mx = 1;
        for (int k = 0; k < 3; ++k)
            for (int i = 1; i < 255; ++i) mx = std::max(mx, hist[k][i]);
        grid(cv, false, 4);
        GdiFlush();
        const int cols[3][3] = { { 235, 60, 60 }, { 60, 220, 80 }, { 70, 120, 245 } };
        for (int x = 0; x < W; ++x) {
            int bin = x * 256 / W;
            for (int k = 0; k < 3; ++k) {
                double r = std::min(1.0, std::sqrt((double)hist[k][bin] / mx));
                int hgt = (int)(r * (H - 4));
                for (int y = H - 1; y > H - 1 - hgt; --y) put(o, W, x, y, cols[k][0], cols[k][1], cols[k][2]);
            }
        }
    } else { // vectorscopio
        const int R = H / 2 - 6, cx = W / 2, cy = H / 2;
        acc.assign((size_t)W * H, 0);
        for (int y = 0; y < sh; y += 2) {
            const uint8_t* row = src + (size_t)y * sw * 4;
            for (int x = 0; x < sw; x += 2) {
                const uint8_t* p = row + (size_t)x * 4;
                double r = p[2], g = p[1], b = p[0];
                double Y = 0.299 * r + 0.587 * g + 0.114 * b;
                double cb = (b - Y) * 0.564, cr = (r - Y) * 0.713;
                int px = cx + (int)std::lround(cb / 127.5 * R);
                int py = cy - (int)std::lround(cr / 127.5 * R);
                if (px >= 0 && px < W && py >= 0 && py < H) acc[(size_t)py * W + px]++;
            }
        }
        // graticula
        HPEN gp = CreatePen(PS_SOLID, 1, RGB(52, 52, 60));
        HGDIOBJ op = SelectObject(cv.dc, gp), ob = SelectObject(cv.dc, GetStockObject(NULL_BRUSH));
        Ellipse(cv.dc, cx - R, cy - R, cx + R + 1, cy + R + 1);
        Ellipse(cv.dc, cx - R / 2, cy - R / 2, cx + R / 2 + 1, cy + R / 2 + 1);
        SelectObject(cv.dc, op);
        SelectObject(cv.dc, ob);
        DeleteObject(gp);
        lineTo(cv.dc, cx - R, cy, cx + R, cy, RGB(52, 52, 60));
        lineTo(cv.dc, cx, cy - R, cx, cy + R, RGB(52, 52, 60));
        // linea de tono de piel (aprox. 123 grados)
        lineTo(cv.dc, cx, cy, cx + (int)(std::cos(123 * kPi / 180) * R), cy - (int)(std::sin(123 * kPi / 180) * R), RGB(150, 100, 50));
        // objetivos de color
        struct T { int r, g, b; const wchar_t* n; };
        const T ts[6] = { { 255, 0, 0, L"R" }, { 255, 255, 0, L"Yl" }, { 0, 255, 0, L"G" },
                          { 0, 255, 255, L"Cy" }, { 0, 0, 255, L"B" }, { 255, 0, 255, L"Mg" } };
        HFONT of = (HFONT)SelectObject(cv.dc, fSmall);
        SetBkMode(cv.dc, TRANSPARENT);
        SetTextColor(cv.dc, RGB(120, 120, 132));
        for (const T& t : ts) {
            double Y = 0.299 * t.r + 0.587 * t.g + 0.114 * t.b;
            int px = cx + (int)std::lround((t.b - Y) * 0.564 / 127.5 * R);
            int py = cy - (int)std::lround((t.r - Y) * 0.713 / 127.5 * R);
            RECT box{ px - 6, py - 6, px + 7, py + 7 };
            HBRUSH nb = (HBRUSH)GetStockObject(NULL_BRUSH);
            HPEN bp = CreatePen(PS_SOLID, 1, RGB(t.r / 2 + 40, t.g / 2 + 40, t.b / 2 + 40));
            HGDIOBJ o1 = SelectObject(cv.dc, bp), o2 = SelectObject(cv.dc, nb);
            Rectangle(cv.dc, box.left, box.top, box.right, box.bottom);
            SelectObject(cv.dc, o1);
            SelectObject(cv.dc, o2);
            DeleteObject(bp);
            TextOutW(cv.dc, px + 8, py - 6, t.n, (int)wcslen(t.n));
        }
        SelectObject(cv.dc, of);
        GdiFlush();
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                uint32_t n = acc[(size_t)y * W + x];
                if (!n) continue;
                float v = std::min(1.0f, 0.35f + n * 0.25f);
                put(o, W, x, y, (int)(200 * v), (int)(255 * v), (int)(210 * v));
            }
    }
    GdiFlush();
}
