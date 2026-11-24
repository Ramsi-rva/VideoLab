// Tema oscuro (estilo DaVinci Resolve) y funciones de dibujo compartidas sobre CDC (MFC).
#pragma once
#include <algorithm>
#include <string>

#include "Util.h"

namespace Theme {
constexpr COLORREF bg      = RGB(27, 27, 30);
constexpr COLORREF panel   = RGB(37, 37, 41);
constexpr COLORREF panelHi = RGB(46, 46, 51);
constexpr COLORREF header  = RGB(31, 31, 35);
constexpr COLORREF field   = RGB(22, 22, 25);
constexpr COLORREF line    = RGB(13, 13, 15);
constexpr COLORREF border  = RGB(64, 64, 71);
constexpr COLORREF text    = RGB(214, 214, 220);
constexpr COLORREF textDim = RGB(128, 128, 138);
constexpr COLORREF accent  = RGB(232, 78, 60);   // cabezal y seleccion
constexpr COLORREF blue    = RGB(64, 140, 220);  // controles activos
constexpr COLORREF btn     = RGB(54, 54, 60);
constexpr COLORREF btnHot  = RGB(68, 68, 76);
constexpr COLORREF btnDown = RGB(42, 42, 47);
}  // namespace Theme

// Fuentes de la interfaz (objetos MFC CFont creados al iniciar la ventana principal)
inline CFont* fUI = nullptr;
inline CFont* fBold = nullptr;
inline CFont* fSmall = nullptr;
inline CFont* fTitle = nullptr;
inline CFont* fMono = nullptr;

inline CFont* makeFont(int px, int weight, const wchar_t* face = L"Segoe UI") {
    CFont* f = new CFont();
    f->CreateFont(-px, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                  CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, face);
    return f;
}
inline void createFonts() {
    fUI = makeFont(13, FW_NORMAL);
    fBold = makeFont(13, FW_SEMIBOLD);
    fSmall = makeFont(11, FW_NORMAL);
    fTitle = makeFont(36, FW_LIGHT);
    fMono = makeFont(22, FW_NORMAL, L"Consolas");
}
inline void destroyFonts() {
    for (CFont** f : { &fUI, &fBold, &fSmall, &fTitle, &fMono }) { delete *f; *f = nullptr; }
}

// ---- Primitivas de dibujo sobre un CDC de MFC ----
inline void fillRc(CDC* dc, const RECT& r, COLORREF c) {
    CBrush b(c);
    dc->FillRect(&r, &b);
}
inline void lineTo(CDC* dc, int x0, int y0, int x1, int y1, COLORREF c, int w = 1) {
    CPen pen(PS_SOLID, w, c);
    CPen* old = dc->SelectObject(&pen);
    dc->MoveTo(x0, y0);
    dc->LineTo(x1, y1);
    dc->SelectObject(old);
}
inline void textAt(CDC* dc, int x, int y, const std::wstring& s, COLORREF c, CFont* f = nullptr) {
    CFont* old = f ? dc->SelectObject(f) : nullptr;
    dc->SetBkMode(TRANSPARENT);
    dc->SetTextColor(c);
    dc->TextOut(x, y, s.c_str(), (int)s.size());
    if (old) dc->SelectObject(old);
}
inline void textIn(CDC* dc, RECT r, const std::wstring& s, COLORREF c, UINT fmt, CFont* f = nullptr) {
    CFont* old = f ? dc->SelectObject(f) : nullptr;
    dc->SetBkMode(TRANSPARENT);
    dc->SetTextColor(c);
    dc->DrawText(s.c_str(), -1, &r, fmt | DT_NOPREFIX);
    if (old) dc->SelectObject(old);
}
inline void roundBox(CDC* dc, const RECT& r, int rad, COLORREF fill, COLORREF border) {
    CBrush b(fill);
    CPen p(PS_SOLID, 1, border);
    CBrush* ob = dc->SelectObject(&b);
    CPen* op = dc->SelectObject(&p);
    dc->RoundRect(r.left, r.top, r.right, r.bottom, rad, rad);
    dc->SelectObject(ob);
    dc->SelectObject(op);
}
inline void triangle(CDC* dc, POINT a, POINT b, POINT c, COLORREF fill) {
    CBrush br(fill);
    CBrush* ob = dc->SelectObject(&br);
    CGdiObject* op = dc->SelectStockObject(NULL_PEN);
    POINT pts[3] = { a, b, c };
    dc->Polygon(pts, 3);
    dc->SelectObject(ob);
    dc->SelectObject(op);
}
// Elipse con relleno y borde (o sin relleno si fill == CLR_INVALID)
inline void ellipse(CDC* dc, int l, int t, int r, int b, COLORREF fill, COLORREF border, int penW = 1) {
    CPen pen(PS_SOLID, penW, border);
    CBrush br(fill == CLR_INVALID ? RGB(0, 0, 0) : fill);
    CPen* op = dc->SelectObject(&pen);
    CGdiObject* ob = (fill == CLR_INVALID) ? dc->SelectStockObject(NULL_BRUSH) : dc->SelectObject(&br);
    dc->Ellipse(l, t, r, b);
    dc->SelectObject(ob);
    dc->SelectObject(op);
}
