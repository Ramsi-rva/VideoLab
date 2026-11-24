#include "Widgets.h"

#include <algorithm>
#include <cmath>

#include "Theme.h"

static std::vector<std::unique_ptr<CVLWidget>> g_widgets;

static const double kPi = 3.14159265358979323846;

const std::vector<std::unique_ptr<CVLWidget>>& allWidgets() { return g_widgets; }

CVLWidget* findWidget(int id) {
    for (auto& w : g_widgets) if (w->id == id) return w.get();
    return nullptr;
}

void invalidateWidget(CVLWidget* w) {
    if (w && w->GetSafeHwnd()) w->Invalidate(FALSE);
}

void CVLWidget::notify(int ev) {
    if (CWnd* p = GetParent()) p->SendMessage(WM_WIDGET, (WPARAM)id, (LPARAM)ev);
}

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
static void drawIcon(CDC* dc, WIcon ic, const RECT& rc, COLORREF col) {
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

void CVLWidget::paintButton(CDC* dc, const RECT& rc, bool enabled) {
    COLORREF fill = primary ? (down ? RGB(40, 100, 170) : (hover ? RGB(84, 156, 232) : Theme::blue))
                            : (down ? Theme::btnDown : (hover ? Theme::btnHot : Theme::btn));
    COLORREF border = primary ? RGB(40, 100, 170) : Theme::border;
    COLORREF fg = enabled ? (primary ? RGB(255, 255, 255) : Theme::text) : RGB(92, 92, 100);
    if (!enabled) { fill = Theme::panel; border = RGB(52, 52, 58); }
    roundBox(dc, rc, 6, fill, border);
    if (icon != IC_NONE) drawIcon(dc, icon, rc, fg);
    else textIn(dc, rc, text, fg, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, fUI);
}

void CVLWidget::paintToggle(CDC* dc, const RECT& rc, bool enabled) {
    int cy = (rc.top + rc.bottom) / 2;
    RECT box{ rc.left + 2, cy - 8, rc.left + 18, cy + 8 };
    roundBox(dc, box, 4, checked ? Theme::blue : Theme::field, hover ? Theme::textDim : Theme::border);
    if (checked) {
        lineTo(dc, box.left + 4, cy, box.left + 7, cy + 4, RGB(255, 255, 255), 2);
        lineTo(dc, box.left + 7, cy + 4, box.left + 13, cy - 4, RGB(255, 255, 255), 2);
    }
    RECT tr{ rc.left + 26, rc.top, rc.right, rc.bottom };
    textIn(dc, tr, text, enabled ? Theme::text : Theme::textDim, DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, fUI);
}

void CVLWidget::paintSlider(CDC* dc, const RECT& rc, bool enabled) {
    SliderGeo g = sliderGeo(this, rc);
    COLORREF fg = enabled ? Theme::text : RGB(92, 92, 100);
    if (!text.empty()) {
        textIn(dc, g.label, text, enabled ? Theme::textDim : RGB(80, 80, 88), DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, fUI);
        wchar_t b[32];
        wfmt(b, fmt, val);
        RECT vr = g.value;
        roundBox(dc, { vr.left, vr.top + 4, vr.right, vr.bottom - 4 }, 4, Theme::field, Theme::line);
        textIn(dc, vr, b, fg, DT_CENTER | DT_VCENTER | DT_SINGLELINE, fSmall);
    }
    // pista
    RECT track{ g.x0, g.y - 2, g.x1, g.y + 2 };
    fillRc(dc, track, enabled ? RGB(14, 14, 16) : RGB(30, 30, 34));
    int kx = valToX(this, val, g.x0, g.x1);
    int zx = (minv < 0 && maxv > 0) ? valToX(this, 0, g.x0, g.x1) : g.x0;
    RECT fillr{ std::min(kx, zx), g.y - 2, std::max(kx, zx), g.y + 2 };
    if (enabled) fillRc(dc, fillr, Theme::blue);
    if (minv < 0 && maxv > 0) lineTo(dc, zx, g.y - 5, zx, g.y + 5, Theme::border);
    // perilla
    COLORREF kc = !enabled ? RGB(70, 70, 78) : ((down || hover) ? RGB(255, 255, 255) : RGB(206, 206, 212));
    ellipse(dc, kx - 6, g.y - 6, kx + 7, g.y + 7, kc, RGB(20, 20, 22));
}

void CVLWidget::paintDrop(CDC* dc, const RECT& rc, bool enabled) {
    int width = rc.right - rc.left;
    bool hasLabel = !text.empty();
    int lw = hasLabel ? (labelW > 0 ? labelW : std::min(110, width * 4 / 10)) : 0;
    if (hasLabel) {
        RECT lr{ rc.left, rc.top, rc.left + lw, rc.bottom };
        textIn(dc, lr, text, enabled ? Theme::textDim : RGB(80, 80, 88), DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, fUI);
    }
    RECT box{ rc.left + lw, rc.top + 2, rc.right, rc.bottom - 2 };
    roundBox(dc, box, 5, enabled ? (hover || down ? Theme::panelHi : Theme::field) : Theme::panel,
             hover && enabled ? Theme::textDim : Theme::border);
    std::wstring cur = (sel >= 0 && sel < (int)items.size()) ? items[sel] : L"";
    RECT tr{ box.left + 8, box.top, box.right - 22, box.bottom };
    textIn(dc, tr, cur, enabled ? Theme::text : RGB(92, 92, 100), DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, fUI);
    int ax = box.right - 12, ay = (box.top + box.bottom) / 2;
    triangle(dc, { ax - 4, ay - 2 }, { ax + 4, ay - 2 }, { ax, ay + 3 }, enabled ? Theme::textDim : RGB(70, 70, 78));
}

void CVLWidget::paintWheel(CDC* dc, const RECT& rc, bool enabled) {
    int width = rc.right - rc.left;
    WheelGeo g = wheelGeo(rc);
    int D = g.R * 2;
    if (!wheelBmp.GetSafeHandle() || wheelD != D) {
        wheelBmp.DeleteObject();
        wheelBmp.Attach(buildWheel(D));
        wheelD = D;
    }
    // titulo
    RECT tr{ rc.left, rc.top + 2, rc.right, rc.top + 20 };
    textIn(dc, tr, text, Theme::text, DT_CENTER | DT_SINGLELINE, fBold);

    // rueda
    if (wheelBmp.GetSafeHandle()) {
        CDC mem;
        mem.CreateCompatibleDC(dc);
        CBitmap* old = mem.SelectObject(&wheelBmp);
        dc->BitBlt(g.cx - g.R, g.cy - g.R, D, D, &mem, 0, 0, SRCCOPY);
        mem.SelectObject(old);
    }
    lineTo(dc, g.cx - g.R + 6, g.cy, g.cx + g.R - 6, g.cy, RGB(70, 70, 76));
    lineTo(dc, g.cx, g.cy - g.R + 6, g.cx, g.cy + g.R - 6, RGB(70, 70, 76));

    // punto
    double pr = g.R * 0.88;
    int px = g.cx + (int)std::lround(wx * pr);
    int py = g.cy - (int)std::lround(wy * pr);
    ellipse(dc, px - 7, py - 7, px + 8, py + 8, CLR_INVALID, enabled ? RGB(255, 255, 255) : RGB(100, 100, 108), 2);

    // master
    int ym = g.ym;
    RECT track{ g.x0, ym - 2, g.x1, ym + 2 };
    fillRc(dc, track, RGB(14, 14, 16));
    int kx = valToX(this, val, g.x0, g.x1), zx = valToX(this, 0, g.x0, g.x1);
    RECT fr{ std::min(kx, zx), ym - 2, std::max(kx, zx), ym + 2 };
    if (enabled) fillRc(dc, fr, Theme::blue);
    lineTo(dc, zx, ym - 5, zx, ym + 5, Theme::border);
    ellipse(dc, kx - 6, ym - 6, kx + 7, ym + 7, drag == 2 ? RGB(255, 255, 255) : RGB(206, 206, 212), RGB(20, 20, 22));

    // valores R G B y master
    double m = std::min(1.0, std::sqrt(wx * wx + wy * wy));
    double th = std::atan2(wy, wx);
    double rgb[3] = { m * std::cos(th), m * std::cos(th - 2 * kPi / 3), m * std::cos(th - 4 * kPi / 3) };
    wchar_t b[96];
    wfmt(b, L"Y %+.2f    R %+.2f   G %+.2f   B %+.2f", val, rgb[0], rgb[1], rgb[2]);
    RECT vr{ rc.left, ym + 9, rc.left + width, rc.bottom };
    textIn(dc, vr, b, Theme::textDim, DT_CENTER | DT_SINGLELINE, fSmall);
}

// ---------------- Mapa de mensajes del control ----------------
BEGIN_MESSAGE_MAP(CVLWidget, CWnd)
    ON_WM_ERASEBKGND()
    ON_WM_PAINT()
    ON_WM_MOUSEMOVE()
    ON_WM_MOUSELEAVE()
    ON_WM_LBUTTONDOWN()
    ON_WM_LBUTTONDBLCLK()
    ON_WM_LBUTTONUP()
    ON_WM_CAPTURECHANGED()
    ON_WM_ENABLE()
END_MESSAGE_MAP()

BOOL CVLWidget::CreateWidget(CWnd* parent) {
    // AfxRegisterWndClass devuelve un bufer estatico que se reutiliza: se guarda una copia del nombre
    static const std::wstring cls = AfxRegisterWndClass(CS_DBLCLKS, ::LoadCursor(nullptr, IDC_ARROW), nullptr, nullptr);
    return Create(cls.c_str(), L"", WS_CHILD, CRect(0, 0, 10, 10), parent, 0);
}

void CVLWidget::OnPaint() {
    CPaintDC pdc(this);
    CRect rc;
    GetClientRect(&rc);
    // doble buffer: se dibuja en memoria y se copia de una vez
    CDC mem;
    mem.CreateCompatibleDC(&pdc);
    CBitmap bmp;
    bmp.CreateCompatibleBitmap(&pdc, std::max(1, rc.Width()), std::max(1, rc.Height()));
    CBitmap* old = mem.SelectObject(&bmp);
    fillRc(&mem, rc, Theme::panel);
    mem.SetBkMode(TRANSPARENT);
    bool en = IsWindowEnabled() != FALSE;
    switch (type) {
    case WT_BUTTON: paintButton(&mem, rc, en); break;
    case WT_TOGGLE: paintToggle(&mem, rc, en); break;
    case WT_SLIDER: paintSlider(&mem, rc, en); break;
    case WT_DROP:   paintDrop(&mem, rc, en); break;
    case WT_WHEEL:  paintWheel(&mem, rc, en); break;
    }
    pdc.BitBlt(0, 0, rc.Width(), rc.Height(), &mem, 0, 0, SRCCOPY);
    mem.SelectObject(old);
}

void CVLWidget::sliderMouse(int x) {
    CRect rc;
    GetClientRect(&rc);
    SliderGeo g = sliderGeo(this, rc);
    double v = xToVal(this, x, g.x0, g.x1);
    if (v != val) { val = v; Invalidate(FALSE); notify(EV_CHANGE); }
}

void CVLWidget::wheelMouse(int x, int y) {
    CRect rc;
    GetClientRect(&rc);
    WheelGeo g = wheelGeo(rc);
    if (drag == 1) {
        double pr = g.R * 0.88;
        double dx = (x - g.cx) / pr, dy = -(y - g.cy) / pr;
        double len = std::sqrt(dx * dx + dy * dy);
        if (len > 1.0) { dx /= len; dy /= len; }
        wx = dx;
        wy = dy;
        Invalidate(FALSE);
        notify(EV_CHANGE);
    } else if (drag == 2) {
        val = xToVal(this, x, g.x0, g.x1);
        Invalidate(FALSE);
        notify(EV_CHANGE);
    }
}

void CVLWidget::OnMouseMove(UINT, CPoint pt) {
    if (!hover) {
        hover = true;
        TRACKMOUSEEVENT t{ sizeof(t), TME_LEAVE, m_hWnd, 0 };
        TrackMouseEvent(&t);
        Invalidate(FALSE);
    }
    if (down || drag) {
        if (type == WT_SLIDER) sliderMouse(pt.x);
        else if (type == WT_WHEEL) wheelMouse(pt.x, pt.y);
    }
}

void CVLWidget::OnMouseLeave() {
    hover = false;
    Invalidate(FALSE);
}

void CVLWidget::mouseDown(CPoint pt, bool dbl) {
    switch (type) {
    case WT_BUTTON:
        down = true;
        SetCapture();
        Invalidate(FALSE);
        break;
    case WT_TOGGLE:
        checked = !checked;
        Invalidate(FALSE);
        notify(EV_CLICK);
        break;
    case WT_DROP:
        if (!items.empty()) openPopup();
        break;
    case WT_SLIDER:
        notify(EV_BEGIN);
        if (dbl) {
            val = def;
            Invalidate(FALSE);
            notify(EV_CHANGE);
            notify(EV_END);
        } else {
            down = true;
            SetCapture();
            sliderMouse(pt.x);
        }
        break;
    case WT_WHEEL: {
        CRect rc;
        GetClientRect(&rc);
        WheelGeo g = wheelGeo(rc);
        bool onMaster = std::abs(pt.y - g.ym) <= 9;
        notify(EV_BEGIN);
        if (dbl) {
            if (onMaster) val = 0; else { wx = 0; wy = 0; }
            Invalidate(FALSE);
            notify(EV_CHANGE);
            notify(EV_END);
        } else {
            drag = onMaster ? 2 : 1;
            down = true;
            SetCapture();
            wheelMouse(pt.x, pt.y);
        }
        break;
    }
    }
}

void CVLWidget::OnLButtonUp(UINT, CPoint pt) {
    bool wasDown = down;
    bool wasDrag = drag != 0;
    down = false;
    drag = 0;
    if (GetCapture() == this) ReleaseCapture();
    Invalidate(FALSE);
    if (type == WT_BUTTON && wasDown) {
        CRect rc;
        GetClientRect(&rc);
        if (rc.PtInRect(pt)) notify(EV_CLICK);
    } else if ((type == WT_SLIDER || type == WT_WHEEL) && (wasDown || wasDrag)) {
        notify(EV_END);
    }
}

void CVLWidget::OnCaptureChanged(CWnd* wnd) {
    if ((down || drag) && wnd != this) { down = false; drag = 0; Invalidate(FALSE); }
    CWnd::OnCaptureChanged(wnd);
}

// ---------------- Desplegable: ventana emergente (CVLPopup) ----------------
class CVLPopup : public CWnd {
public:
    explicit CVLPopup(CVLWidget* owner) : w_(owner), hot_(owner->sel) {}
    BOOL Open(const CRect& r);

protected:
    afx_msg BOOL OnEraseBkgnd(CDC*) { return TRUE; }
    afx_msg void OnPaint();
    afx_msg void OnMouseMove(UINT flags, CPoint pt);
    afx_msg void OnLButtonDown(UINT flags, CPoint pt);
    afx_msg void OnCaptureChanged(CWnd* wnd);
    void PostNcDestroy() override { delete this; }   // la ventana emergente se libera sola
    DECLARE_MESSAGE_MAP()

public:
    static const int kItemH = 26;

private:
    CVLWidget* w_;
    int hot_;
    bool closing_ = false;
};

BEGIN_MESSAGE_MAP(CVLPopup, CWnd)
    ON_WM_ERASEBKGND()
    ON_WM_PAINT()
    ON_WM_MOUSEMOVE()
    ON_WM_LBUTTONDOWN()
    ON_WM_CAPTURECHANGED()
END_MESSAGE_MAP()

BOOL CVLPopup::Open(const CRect& r) {
    static const std::wstring cls = AfxRegisterWndClass(CS_DROPSHADOW, ::LoadCursor(nullptr, IDC_ARROW), nullptr, nullptr);
    if (!CreateEx(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE, cls.c_str(), L"", WS_POPUP,
                  r.left, r.top, r.Width(), r.Height(), w_->GetParent()->GetSafeHwnd(), nullptr))
        return FALSE;
    ShowWindow(SW_SHOWNOACTIVATE);
    SetCapture();
    return TRUE;
}

void CVLPopup::OnMouseMove(UINT, CPoint pt) {
    int idx = pt.y / kItemH;
    if (idx != hot_) { hot_ = idx; Invalidate(FALSE); }
}

void CVLPopup::OnLButtonDown(UINT, CPoint pt) { // el soltar del clic que abrio el menu se ignora a proposito
    if (closing_) return;
    CRect rc;
    GetClientRect(&rc);
    int idx = pt.y / kItemH;
    CVLWidget* w = w_;
    bool ok = rc.PtInRect(pt) && idx >= 0 && idx < (int)w->items.size();
    closing_ = true;
    ReleaseCapture();
    DestroyWindow();                 // 'this' se borra en PostNcDestroy: no usar miembros despues
    if (ok) {
        w->sel = idx;
        invalidateWidget(w);
        w->notify(EV_CLICK);
    }
}

void CVLPopup::OnCaptureChanged(CWnd* wnd) {
    if (!closing_) { closing_ = true; DestroyWindow(); }
}

void CVLPopup::OnPaint() {
    CPaintDC dc(this);
    CRect rc;
    GetClientRect(&rc);
    fillRc(&dc, rc, Theme::field);
    for (int i = 0; i < (int)w_->items.size(); ++i) {
        RECT ir{ rc.left + 2, i * kItemH + 2, rc.right - 2, (i + 1) * kItemH };
        if (i == hot_) fillRc(&dc, ir, Theme::blue);
        RECT tr{ ir.left + 22, ir.top, ir.right - 6, ir.bottom };
        textIn(&dc, tr, w_->items[i], i == hot_ ? RGB(255, 255, 255) : Theme::text,
               DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, fUI);
        if (i == w_->sel) {
            int cy = (ir.top + ir.bottom) / 2;
            COLORREF ck = i == hot_ ? RGB(255, 255, 255) : Theme::blue;
            lineTo(&dc, ir.left + 7, cy, ir.left + 10, cy + 3, ck, 2);
            lineTo(&dc, ir.left + 10, cy + 3, ir.left + 15, cy - 3, ck, 2);
        }
    }
    CPen bp(PS_SOLID, 1, Theme::border);
    CPen* op = dc.SelectObject(&bp);
    CGdiObject* ob = dc.SelectStockObject(NULL_BRUSH);
    dc.Rectangle(&rc);
    dc.SelectObject(ob);
    dc.SelectObject(op);
}

void CVLWidget::openPopup() {
    CRect wr;
    GetWindowRect(&wr);
    int count = (int)items.size();
    int width = std::max<int>(wr.Width(), 150);
    int height = count * CVLPopup::kItemH + 4;
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    GetMonitorInfo(MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST), &mi);
    int x = std::min<int>(wr.left, mi.rcWork.right - width);
    int y = wr.bottom;
    if (y + height > mi.rcWork.bottom) y = wr.top - height;
    CVLPopup* p = new CVLPopup(this);   // se borra solo en PostNcDestroy al cerrarse
    p->Open(CRect(x, y, x + width, y + height));
}

// ---------------- Fabrica ----------------
CVLWidget* createWidget(CWnd* parent, WType type, int id, const wchar_t* text) {
    auto w = std::make_unique<CVLWidget>();
    w->type = type;
    w->id = id;
    w->text = text ? text : L"";
    w->CreateWidget(parent);
    CVLWidget* raw = w.get();
    g_widgets.push_back(std::move(w));
    return raw;
}
