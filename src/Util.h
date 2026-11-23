// Utilidades comunes: cabeceras MFC/Win32 y formato de texto ancho.
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
// Interfaz MFC (Microsoft Foundation Classes). afxwin.h ya incluye windows.h.
#include <afxwin.h>
#include <afxdlgs.h>   // CFileDialog

#include <cstdarg>
#include <cstdio>
#include <cwchar>
#include <string>

// Equivalente portable de swprintf_s con deduccion del tamano del buffer.
template <size_t N>
inline int wfmt(wchar_t (&buf)[N], const wchar_t* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = _vsnwprintf(buf, N, fmt, ap);
    va_end(ap);
    buf[N - 1] = 0;
    return r;
}
