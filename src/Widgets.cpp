#include "Widgets.h"

#include <windowsx.h>

#include <algorithm>
#include <cmath>

#include "Theme.h"

static const wchar_t* kWidgetClass = L"VLWidget";
static const wchar_t* kPopupClass = L"VLPopup";
static std::vector<std::unique_ptr<Widget>> g_widgets;

static const double kPi = 3.14159265358979323846;

const std::vector<std::unique_ptr<Widget>>& allWidgets() { return g_widgets; }

Widget* findWidget(int id) {
    for (auto& w : g_widgets) if (w->id == id) return w.get();
    return nullptr;
}

void invalidateWidget(Widget* w) {
    if (w && w->hwnd) InvalidateRect(w->hwnd, nullptr, FALSE);
}

static Widget* W(HWND h) { return (Widget*)GetWindowLongPtrW(h, GWLP_USERDATA); }
static void notify(Widget* w, int ev) { SendMessageW(GetParent(w->hwnd), WM_WIDGET, (WPARAM)w->id, (LPARAM)ev); }

// ---------------- Geometria ----------------
struct SliderGeo { int x0, x1, y; RECT label, value; };

static SliderGeo sliderGeo(const Widget* w, const RECT& rc) {
    SliderGeo g{};
    int width = rc.right - rc.left;
    bool hasLabel = !w->text.empty();
    int lw = hasLabel ? (w->labelW > 0 ? w->labelW : std::min(110, width * 4 / 10)) : 0;
    int vw = hasLabel ? 46 : 0;
    g.label = { rc.left, rc.top, rc.left + lw, rc.bottom };
    g.value = { rc.right - vw, rc.top, rc.right, rc.bottom };
    g.x0 = rc.left + lw + 6;
    g.x1 = rc.right - vw - 6;
    g.y = (rc.top + rc.bottom) / 2;
    return g;
}

struct WheelGeo { int cx, cy, R, ym, x0, x1; };

static WheelGeo wheelGeo(const RECT& rc) {
    int w = rc.right - rc.left, h = rc.bottom - rc.top;
    int D = std::max(40, std::min(w - 8, h - 70));
    WheelGeo g{};
    g.R = D / 2;
    g.cx = w / 2;
    g.cy = 22 + g.R;
    g.ym = g.cy + g.R + 16;
    g.x0 = g.cx - g.R;
    g.x1 = g.cx + g.R;
    return g;
}

static double snap(const Widget* w, double v) {
    if (w->step > 0) v = std::round(v / w->step) * w->step;
    return std::clamp(v, w->minv, w->maxv);
}

static double xToVal(const Widget* w, int x, int x0, int x1) {
    double t = std::clamp((double)(x - x0) / std::max(1, x1 - x0), 0.0, 1.0);
    return snap(w, w->minv + t * (w->maxv - w->minv));
}

static int valToX(const Widget* w, double v, int x0, int x1) {
    double t = (v - w->minv) / (w->maxv - w->minv);
    return x0 + (int)std::lround(std::clamp(t, 0.0, 1.0) * (x1 - x0));
}

// ---------------- Rueda de color: imagen base ----------------
static void hsv2rgb(double h, double s, double v, int& r, int& g, int& b) {
    h = std::fmod(h, 360.0);
    if (h < 0) h += 360.0;
    double c = v * s, x = c * (1 - std::fabs(std::fmod(h / 60.0, 2.0) - 1)), m = v - c;
    double rr = 0, gg = 0, bb = 0;
    if (h < 60) { rr = c; gg = x; }
    else if (h < 120) { rr = x; gg = c; }
    else if (h < 180) { gg = c; bb = x; }
    else if (h < 240) { gg = x; bb = c; }
    else if (h < 300) { rr = x; bb = c; }
    else { rr = c; bb = x; }
    r = (int)((rr + m) * 255); g = (int)((gg + m) * 255); b = (int)((bb + m) * 255);
}

static HBITMAP buildWheel(int D) {
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = D;
    bi.bmiHeader.biHeight = -D;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    uint8_t* bits = nullptr;
    HDC scr = GetDC(nullptr);
    HBITMAP bmp = CreateDIBSection(scr, &bi, DIB_RGB_COLORS, (void**)&bits, nullptr, 0);
    ReleaseDC(nullptr, scr);
    if (!bmp || !bits) return bmp;

    const int bgR = GetRValue(Theme::panel), bgG = GetGValue(Theme::panel), bgB = GetBValue(Theme::panel);
    const double R = D / 2.0 - 1.0, cx = (D - 1) / 2.0, cy = (D - 1) / 2.0;
    const double ringIn = R * 0.88;
    for (int y = 0; y < D; ++y) {
        for (int x = 0; x < D; ++x) {
            double dx = x - cx, dy = cy - y; // y hacia arriba
            double r = std::sqrt(dx * dx + dy * dy);
            double ang = std::atan2(dy, dx) * 180.0 / kPi;
            int pr = bgR, pg = bgG, pb = bgB;
            if (r <= R + 0.7) {
                double cover = std::clamp(R + 0.7 - r, 0.0, 1.0);
                int cr, cg, cb;
                if (r >= ringIn) {                       // anillo de tonos
                    hsv2rgb(ang, 0.85, 0.80, cr, cg, cb);
                } else {                                 // interior oscuro con tinte tenue
                    double k = 0.10 + 0.16 * (r / ringIn);
                    hsv2rgb(ang, 1.0, k, cr, cg, cb);
                    cr += 14; cg += 14; cb += 16;
                }
                pr = (int)(bgR + (cr - bgR) * cover);
                pg = (int)(bgG + (cg - bgG) * cover);
                pb = (int)(bgB + (cb - bgB) * cover);
            }
            uint8_t* p = bits + ((size_t)y * D + x) * 4;
            p[0] = (uint8_t)pb; p[1] = (uint8_t)pg; p[2] = (uint8_t)pr; p[3] = 255;
        }
    }
    return bmp;
}

// ---------------- Dibujo ----------------
static void drawIcon(HDC dc, WIcon ic, const RECT& rc, COLORREF col) {
    int cx = (rc.left + rc.right) / 2, cy = (rc.top + rc.bottom) / 2;
    switch (ic) {
    case IC_PLAY:
        triangle(dc, { cx - 5, cy - 8 }, { cx - 5, cy + 8 }, { cx + 8, cy }, col);
        break;
    case IC_PAUSE: {
        RECT a{ cx - 6, cy - 8, cx - 2, cy + 8 }, b{ cx + 2, cy - 8, cx + 6, cy + 8 };
        fillRc(dc, a, col);
        fillRc(dc, b, col);
        break;
    }
    case IC_PREV: {
        RECT bar{ cx - 8, cy - 8, cx - 5, cy + 8 };
        fillRc(dc, bar, col);
        triangle(dc, { cx + 7, cy - 8 }, { cx + 7, cy + 8 }, { cx - 4, cy }, col);
        break;
    }
    case IC_NEXT: {
        RECT bar{ cx + 5, cy - 8, cx + 8, cy + 8 };
        fillRc(dc, bar, col);
        triangle(dc, { cx - 7, cy - 8 }, { cx - 7, cy + 8 }, { cx + 4, cy }, col);
        break;
    }
    default: break;
    }
}

static void paintButton(HDC dc, Widget* w, const RECT& rc, bool enabled) {
    COLORREF fill = w->primary ? (w->down ? RGB(40, 100, 170) : (w->hover ? RGB(84, 156, 232) : Theme::blue))
                               : (w->down ? Theme::btnDown : (w->hover ? Theme::btnHot : Theme::btn));
    COLORREF border = w->primary ? RGB(40, 100, 170) : Theme::border;
    COLORREF fg = enabled ? (w->primary ? RGB(255, 255, 255) : Theme::text) : RGB(92, 92, 100);
    if (!enabled) { fill = Theme::panel; border = RGB(52, 52, 58); }
    roundBox(dc, rc, 6, fill, border);
    if (w->icon != IC_NONE) drawIcon(dc, w->icon, rc, fg);
    else textIn(dc, rc, w->text, fg, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, fUI);
}

static void paintToggle(HDC dc, Widget* w, const RECT& rc, bool enabled) {
    int cy = (rc.top + rc.bottom) / 2;
    RECT box{ rc.left + 2, cy - 8, rc.left + 18, cy + 8 };
    roundBox(dc, box, 4, w->checked ? Theme::blue : Theme::field, w->hover ? Theme::textDim : Theme::border);
    if (w->checked) {
        lineTo(dc, box.left + 4, cy, box.left + 7, cy + 4, RGB(255, 255, 255), 2);
        lineTo(dc, box.left + 7, cy + 4, box.left + 13, cy - 4, RGB(255, 255, 255), 2);
    }
    RECT tr{ rc.left + 26, rc.top, rc.right, rc.bottom };
    textIn(dc, tr, w->text, enabled ? Theme::text : Theme::textDim, DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, fUI);
}

static void paintSlider(HDC dc, Widget* w, const RECT& rc, bool enabled) {
    SliderGeo g = sliderGeo(w, rc);
    COLORREF fg = enabled ? Theme::text : RGB(92, 92, 100);
    if (!w->text.empty()) {
        textIn(dc, g.label, w->text, enabled ? Theme::textDim : RGB(80, 80, 88), DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, fUI);
        wchar_t b[32];
        wfmt(b, w->fmt, w->val);
        RECT vr = g.value;
        roundBox(dc, { vr.left, vr.top + 4, vr.right, vr.bottom - 4 }, 4, Theme::field, Theme::line);
        textIn(dc, vr, b, fg, DT_CENTER | DT_VCENTER | DT_SINGLELINE, fSmall);
    }
    // pista
    RECT track{ g.x0, g.y - 2, g.x1, g.y + 2 };
    fillRc(dc, track, enabled ? RGB(14, 14, 16) : RGB(30, 30, 34));
    int kx = valToX(w, w->val, g.x0, g.x1);
    int zx = (w->minv < 0 && w->maxv > 0) ? valToX(w, 0, g.x0, g.x1) : g.x0;
    RECT fillr{ std::min(kx, zx), g.y - 2, std::max(kx, zx), g.y + 2 };
    if (enabled) fillRc(dc, fillr, Theme::blue);
    if (w->minv < 0 && w->maxv > 0) lineTo(dc, zx, g.y - 5, zx, g.y + 5, Theme::border);
    // perilla
    COLORREF kc = !enabled ? RGB(70, 70, 78) : ((w->down || w->hover) ? RGB(255, 255, 255) : RGB(206, 206, 212));
    HBRUSH kb = CreateSolidBrush(kc);
    HPEN kp = CreatePen(PS_SOLID, 1, RGB(20, 20, 22));
    HGDIOBJ ob = SelectObject(dc, kb), op = SelectObject(dc, kp);
    Ellipse(dc, kx - 6, g.y - 6, kx + 7, g.y + 7);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(kb);
    DeleteObject(kp);
}

static void paintDrop(HDC dc, Widget* w, const RECT& rc, bool enabled) {
    int width = rc.right - rc.left;
    bool hasLabel = !w->text.empty();
    int lw = hasLabel ? (w->labelW > 0 ? w->labelW : std::min(110, width * 4 / 10)) : 0;
    if (hasLabel) {
        RECT lr{ rc.left, rc.top, rc.left + lw, rc.bottom };
        textIn(dc, lr, w->text, enabled ? Theme::textDim : RGB(80, 80, 88), DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, fUI);
    }
    RECT box{ rc.left + lw, rc.top + 2, rc.right, rc.bottom - 2 };
    roundBox(dc, box, 5, enabled ? (w->hover || w->down ? Theme::panelHi : Theme::field) : Theme::panel,
             w->hover && enabled ? Theme::textDim : Theme::border);
    std::wstring cur = (w->sel >= 0 && w->sel < (int)w->items.size()) ? w->items[w->sel] : L"";
    RECT tr{ box.left + 8, box.top, box.right - 22, box.bottom };
    textIn(dc, tr, cur, enabled ? Theme::text : RGB(92, 92, 100), DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, fUI);
    int ax = box.right - 12, ay = (box.top + box.bottom) / 2;
    triangle(dc, { ax - 4, ay - 2 }, { ax + 4, ay - 2 }, { ax, ay + 3 }, enabled ? Theme::textDim : RGB(70, 70, 78));
}

static void paintWheel(HDC dc, Widget* w, const RECT& rc, bool enabled) {
    int width = rc.right - rc.left;
    WheelGeo g = wheelGeo(rc);
    int D = g.R * 2;
    if (!w->wheelBmp || w->wheelD != D) {
        if (w->wheelBmp) DeleteObject(w->wheelBmp);
        w->wheelBmp = buildWheel(D);
        w->wheelD = D;
    }
    // titulo
    RECT tr{ rc.left, rc.top + 2, rc.right, rc.top + 20 };
    textIn(dc, tr, w->text, Theme::text, DT_CENTER | DT_SINGLELINE, fBold);

    // rueda
    if (w->wheelBmp) {
        HDC mem = CreateCompatibleDC(dc);
        HGDIOBJ old = SelectObject(mem, w->wheelBmp);
        BitBlt(dc, g.cx - g.R, g.cy - g.R, D, D, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old);
        DeleteDC(mem);
    }
    lineTo(dc, g.cx - g.R + 6, g.cy, g.cx + g.R - 6, g.cy, RGB(70, 70, 76));
    lineTo(dc, g.cx, g.cy - g.R + 6, g.cx, g.cy + g.R - 6, RGB(70, 70, 76));

    // punto
    double pr = g.R * 0.88;
    int px = g.cx + (int)std::lround(w->wx * pr);
    int py = g.cy - (int)std::lround(w->wy * pr);
    HBRUSH nb = (HBRUSH)GetStockObject(NULL_BRUSH);
    HPEN pw = CreatePen(PS_SOLID, 2, enabled ? RGB(255, 255, 255) : RGB(100, 100, 108));
    HGDIOBJ ob = SelectObject(dc, nb), op = SelectObject(dc, pw);
    Ellipse(dc, px - 7, py - 7, px + 8, py + 8);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(pw);

    // master
    int ym = g.ym;
    RECT track{ g.x0, ym - 2, g.x1, ym + 2 };
    fillRc(dc, track, RGB(14, 14, 16));
    int kx = valToX(w, w->val, g.x0, g.x1), zx = valToX(w, 0, g.x0, g.x1);
    RECT fr{ std::min(kx, zx), ym - 2, std::max(kx, zx), ym + 2 };
    if (enabled) fillRc(dc, fr, Theme::blue);
    lineTo(dc, zx, ym - 5, zx, ym + 5, Theme::border);
    HBRUSH kb = CreateSolidBrush(w->drag == 2 ? RGB(255, 255, 255) : RGB(206, 206, 212));
    HPEN kp = CreatePen(PS_SOLID, 1, RGB(20, 20, 22));
    HGDIOBJ ob2 = SelectObject(dc, kb), op2 = SelectObject(dc, kp);
    Ellipse(dc, kx - 6, ym - 6, kx + 7, ym + 7);
    SelectObject(dc, ob2);
    SelectObject(dc, op2);
    DeleteObject(kb);
    DeleteObject(kp);

    // valores R G B y master
    double m = std::min(1.0, std::sqrt(w->wx * w->wx + w->wy * w->wy));
    double th = std::atan2(w->wy, w->wx);
    double rgb[3] = { m * std::cos(th), m * std::cos(th - 2 * kPi / 3), m * std::cos(th - 4 * kPi / 3) };
    wchar_t b[96];
    wfmt(b, L"Y %+.2f    R %+.2f   G %+.2f   B %+.2f", w->val, rgb[0], rgb[1], rgb[2]);
    RECT vr{ rc.left, ym + 9, rc.left + width, rc.bottom };
    textIn(dc, vr, b, Theme::textDim, DT_CENTER | DT_SINGLELINE, fSmall);
}

static void paintWidget(HWND h) {
    Widget* w = W(h);
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(h, &ps);
    RECT rc;
    GetClientRect(h, &rc);
    if (w) {
        HDC dc = CreateCompatibleDC(hdc);
        HBITMAP bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
        HGDIOBJ old = SelectObject(dc, bmp);
        fillRc(dc, rc, Theme::panel);
        SetBkMode(dc, TRANSPARENT);
        bool en = IsWindowEnabled(h) != FALSE;
        switch (w->type) {
        case WT_BUTTON: paintButton(dc, w, rc, en); break;
        case WT_TOGGLE: paintToggle(dc, w, rc, en); break;
        case WT_SLIDER: paintSlider(dc, w, rc, en); break;
        case WT_DROP:   paintDrop(dc, w, rc, en); break;
        case WT_WHEEL:  paintWheel(dc, w, rc, en); break;
        }
        BitBlt(hdc, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
        SelectObject(dc, old);
        DeleteObject(bmp);
        DeleteDC(dc);
    }
    EndPaint(h, &ps);
}

// ---------------- Desplegable: ventana emergente ----------------
struct PopupData { Widget* w; int hot; int itemH; bool closing; };

static LRESULT CALLBACK PopupProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    PopupData* d = (PopupData*)GetWindowLongPtrW(h, GWLP_USERDATA);
    switch (m) {
    case WM_MOUSEMOVE:
        if (d) {
            int idx = GET_Y_LPARAM(lp) / d->itemH;
            if (idx != d->hot) { d->hot = idx; InvalidateRect(h, nullptr, FALSE); }
        }
        return 0;
    case WM_LBUTTONDOWN: { // el soltar del clic que abrio el menu se ignora a proposito
        if (!d || d->closing) return 0;
        RECT rc;
        GetClientRect(h, &rc);
        POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        bool inside = PtInRect(&rc, pt) != FALSE;
        int idx = pt.y / d->itemH;
        Widget* w = d->w;
        bool ok = inside && idx >= 0 && idx < (int)w->items.size();
        d->closing = true;
        ReleaseCapture();
        DestroyWindow(h);
        if (ok) {
            w->sel = idx;
            invalidateWidget(w);
            notify(w, EV_CLICK);
        }
        return 0;
    }
    case WM_CAPTURECHANGED:
        if (d && !d->closing) { d->closing = true; DestroyWindow(h); }
        return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT rc;
        GetClientRect(h, &rc);
        fillRc(dc, rc, Theme::field);
        if (d) {
            for (int i = 0; i < (int)d->w->items.size(); ++i) {
                RECT ir{ rc.left + 2, i * d->itemH + 2, rc.right - 2, (i + 1) * d->itemH };
                if (i == d->hot) fillRc(dc, ir, Theme::blue);
                RECT tr{ ir.left + 22, ir.top, ir.right - 6, ir.bottom };
                textIn(dc, tr, d->w->items[i], i == d->hot ? RGB(255, 255, 255) : Theme::text,
                       DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, fUI);
                if (i == d->w->sel) {
                    int cy = (ir.top + ir.bottom) / 2;
                    lineTo(dc, ir.left + 7, cy, ir.left + 10, cy + 3, i == d->hot ? RGB(255, 255, 255) : Theme::blue, 2);
                    lineTo(dc, ir.left + 10, cy + 3, ir.left + 15, cy - 3, i == d->hot ? RGB(255, 255, 255) : Theme::blue, 2);
                }
            }
        }
        HBRUSH nb = (HBRUSH)GetStockObject(NULL_BRUSH);
        HPEN bp = CreatePen(PS_SOLID, 1, Theme::border);
        HGDIOBJ ob = SelectObject(dc, nb), op = SelectObject(dc, bp);
        Rectangle(dc, rc.left, rc.top, rc.right, rc.bottom);
        SelectObject(dc, ob);
        SelectObject(dc, op);
        DeleteObject(bp);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_NCDESTROY:
        delete d;
        SetWindowLongPtrW(h, GWLP_USERDATA, 0);
        break;
    }
    return DefWindowProcW(h, m, wp, lp);
}

static void openPopup(Widget* w) {
    RECT wr;
    GetWindowRect(w->hwnd, &wr);
    const int itemH = 26;
    int count = (int)w->items.size();
    int width = std::max<int>(wr.right - wr.left, 150);
    int height = count * itemH + 4;
    HMONITOR mon = MonitorFromWindow(w->hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(mon, &mi);
    int x = std::min<int>(wr.left, mi.rcWork.right - width);
    int y = wr.bottom;
    if (y + height > mi.rcWork.bottom) y = wr.top - height;

    PopupData* d = new PopupData{ w, w->sel, itemH, false };
    HWND p = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE, kPopupClass, L"", WS_POPUP,
                             x, y, width, height, GetParent(w->hwnd), nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!p) { delete d; return; }
    SetWindowLongPtrW(p, GWLP_USERDATA, (LONG_PTR)d);
    ShowWindow(p, SW_SHOWNOACTIVATE);
    SetCapture(p);
}

// ---------------- Mensajes del control ----------------
static void sliderMouse(HWND h, Widget* w, int x) {
    RECT rc;
    GetClientRect(h, &rc);
    SliderGeo g = sliderGeo(w, rc);
    double v = xToVal(w, x, g.x0, g.x1);
    if (v != w->val) { w->val = v; invalidateWidget(w); notify(w, EV_CHANGE); }
}

static void wheelMouse(HWND h, Widget* w, int x, int y) {
    RECT rc;
    GetClientRect(h, &rc);
    WheelGeo g = wheelGeo(rc);
    if (w->drag == 1) {
        double pr = g.R * 0.88;
        double dx = (x - g.cx) / pr, dy = -(y - g.cy) / pr;
        double len = std::sqrt(dx * dx + dy * dy);
        if (len > 1.0) { dx /= len; dy /= len; }
        w->wx = dx;
        w->wy = dy;
        invalidateWidget(w);
        notify(w, EV_CHANGE);
    } else if (w->drag == 2) {
        double v = xToVal(w, x, g.x0, g.x1);
        w->val = v;
        invalidateWidget(w);
        notify(w, EV_CHANGE);
    }
}

static LRESULT CALLBACK WidgetProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    Widget* w = W(h);
    switch (m) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: paintWidget(h); return 0;
    case WM_MOUSEMOVE: {
        if (!w) break;
        if (!w->hover) {
            w->hover = true;
            TRACKMOUSEEVENT t{ sizeof(t), TME_LEAVE, h, 0 };
            TrackMouseEvent(&t);
            invalidateWidget(w);
        }
        if (w->down || w->drag) {
            int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
            if (w->type == WT_SLIDER) sliderMouse(h, w, x);
            else if (w->type == WT_WHEEL) wheelMouse(h, w, x, y);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        if (w) { w->hover = false; invalidateWidget(w); }
        return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK: {
        if (!w) break;
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        bool dbl = (m == WM_LBUTTONDBLCLK);
        switch (w->type) {
        case WT_BUTTON:
            w->down = true;
            SetCapture(h);
            invalidateWidget(w);
            break;
        case WT_TOGGLE:
            w->checked = !w->checked;
            invalidateWidget(w);
            notify(w, EV_CLICK);
            break;
        case WT_DROP:
            if (!w->items.empty()) openPopup(w);
            break;
        case WT_SLIDER:
            notify(w, EV_BEGIN);
            if (dbl) {
                w->val = w->def;
                invalidateWidget(w);
                notify(w, EV_CHANGE);
                notify(w, EV_END);
            } else {
                w->down = true;
                SetCapture(h);
                sliderMouse(h, w, x);
            }
            break;
        case WT_WHEEL: {
            RECT rc;
            GetClientRect(h, &rc);
            WheelGeo g = wheelGeo(rc);
            bool onMaster = std::abs(y - g.ym) <= 9;
            notify(w, EV_BEGIN);
            if (dbl) {
                if (onMaster) w->val = 0; else { w->wx = 0; w->wy = 0; }
                invalidateWidget(w);
                notify(w, EV_CHANGE);
                notify(w, EV_END);
            } else {
                w->drag = onMaster ? 2 : 1;
                w->down = true;
                SetCapture(h);
                wheelMouse(h, w, x, y);
            }
            break;
        }
        }
        return 0;
    }
    case WM_LBUTTONUP: {
        if (!w) break;
        bool wasDown = w->down;
        bool wasDrag = w->drag != 0;
        w->down = false;
        w->drag = 0;
        if (GetCapture() == h) ReleaseCapture();
        invalidateWidget(w);
        if (w->type == WT_BUTTON && wasDown) {
            RECT rc;
            GetClientRect(h, &rc);
            POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            if (PtInRect(&rc, pt)) notify(w, EV_CLICK);
        } else if ((w->type == WT_SLIDER || w->type == WT_WHEEL) && (wasDown || wasDrag)) {
            notify(w, EV_END);
        }
        return 0;
    }
    case WM_CAPTURECHANGED:
        if (w && (w->down || w->drag) && (HWND)lp != h) { w->down = false; w->drag = 0; invalidateWidget(w); }
        return 0;
    case WM_ENABLE:
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    case WM_NCDESTROY:
        if (w && w->wheelBmp) { DeleteObject(w->wheelBmp); w->wheelBmp = nullptr; }
        break;
    }
    return DefWindowProcW(h, m, wp, lp);
}

void registerWidgetClasses(HINSTANCE inst) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = WidgetProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kWidgetClass;
    RegisterClassExW(&wc);

    WNDCLASSEXW pc{};
    pc.cbSize = sizeof(pc);
    pc.style = CS_DROPSHADOW;
    pc.lpfnWndProc = PopupProc;
    pc.hInstance = inst;
    pc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    pc.lpszClassName = kPopupClass;
    RegisterClassExW(&pc);
}

Widget* createWidget(HWND parent, WType type, int id, const wchar_t* text) {
    auto w = std::make_unique<Widget>();
    w->type = type;
    w->id = id;
    w->text = text ? text : L"";
    w->hwnd = CreateWindowExW(0, kWidgetClass, L"", WS_CHILD, 0, 0, 10, 10, parent, nullptr,
                              GetModuleHandleW(nullptr), nullptr);
    SetWindowLongPtrW(w->hwnd, GWLP_USERDATA, (LONG_PTR)w.get());
    Widget* raw = w.get();
    g_widgets.push_back(std::move(w));
    return raw;
}
