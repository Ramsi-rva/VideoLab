// Tema oscuro (estilo DaVinci Resolve) y funciones de dibujo GDI compartidas.
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

inline HFONT fUI = nullptr, fBold = nullptr, fSmall = nullptr, fTitle = nullptr, fMono = nullptr;

inline HFONT makeFont(int px, int weight, const wchar_t* face = L"Segoe UI") {
    return CreateFontW(-px, 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, face);
}
inline void createFonts() {
    fUI = makeFont(13, FW_NORMAL);
    fBold = makeFont(13, FW_SEMIBOLD);
    fSmall = makeFont(11, FW_NORMAL);
    fTitle = makeFont(36, FW_LIGHT);
    fMono = makeFont(22, FW_NORMAL, L"Consolas");
}
inline void destroyFonts() {
    for (HFONT f : { fUI, fBold, fSmall, fTitle, fMono }) if (f) DeleteObject(f);
    fUI = fBold = fSmall = fTitle = fMono = nullptr;
}

inline void fillRc(HDC dc, const RECT& r, COLORREF c) {
    HBRUSH b = CreateSolidBrush(c);
    FillRect(dc, &r, b);
    DeleteObject(b);
}
inline void lineTo(HDC dc, int x0, int y0, int x1, int y1, COLORREF c, int w = 1) {
    HPEN p = CreatePen(PS_SOLID, w, c);
    HGDIOBJ o = SelectObject(dc, p);
    MoveToEx(dc, x0, y0, nullptr);
    LineTo(dc, x1, y1);
    SelectObject(dc, o);
    DeleteObject(p);
}
inline void textAt(HDC dc, int x, int y, const std::wstring& s, COLORREF c, HFONT f = nullptr) {
    HGDIOBJ o = f ? SelectObject(dc, f) : nullptr;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, c);
    TextOutW(dc, x, y, s.c_str(), (int)s.size());
    if (o) SelectObject(dc, o);
}
inline void textIn(HDC dc, RECT r, const std::wstring& s, COLORREF c, UINT fmt, HFONT f = nullptr) {
    HGDIOBJ o = f ? SelectObject(dc, f) : nullptr;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, c);
    DrawTextW(dc, s.c_str(), -1, &r, fmt | DT_NOPREFIX);
    if (o) SelectObject(dc, o);
}
inline void roundBox(HDC dc, const RECT& r, int rad, COLORREF fill, COLORREF border) {
    HBRUSH b = CreateSolidBrush(fill);
    HPEN p = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ ob = SelectObject(dc, b), op = SelectObject(dc, p);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, rad, rad);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(b);
    DeleteObject(p);
}
inline void triangle(HDC dc, POINT a, POINT b, POINT c, COLORREF fill) {
    HBRUSH br = CreateSolidBrush(fill);
    HGDIOBJ ob = SelectObject(dc, br), op = SelectObject(dc, GetStockObject(NULL_PEN));
    POINT pts[3] = { a, b, c };
    Polygon(dc, pts, 3);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(br);
}
