// Controles propios con tema oscuro: boton, interruptor, slider, rueda de color y desplegable.
#pragma once
#include "Util.h"

#include <memory>
#include <string>
#include <vector>

#define WM_WIDGET (WM_APP + 10) // al padre: wParam = id, lParam = WEvent

enum WEvent { EV_BEGIN = 1, EV_CHANGE, EV_END, EV_CLICK };
enum WType { WT_BUTTON, WT_TOGGLE, WT_SLIDER, WT_WHEEL, WT_DROP };
enum WIcon { IC_NONE, IC_PLAY, IC_PAUSE, IC_PREV, IC_NEXT };

struct Widget {
    HWND hwnd = nullptr;
    WType type = WT_BUTTON;
    int id = 0;
    std::wstring text;                  // etiqueta
    WIcon icon = IC_NONE;
    bool primary = false;               // boton destacado (azul)

    // slider (y master de la rueda)
    double minv = 0, maxv = 100, val = 0, def = 0, step = 1;
    const wchar_t* fmt = L"%.0f";
    int labelW = 0;                     // 0 = automatico

    // rueda de color
    double wx = 0, wy = 0;
    HBITMAP wheelBmp = nullptr;
    int wheelD = 0;

    // desplegable
    std::vector<std::wstring> items;
    int sel = 0;

    // interruptor
    bool checked = false;

    // estado interno
    bool hover = false, down = false;
    int drag = 0;

    // control de visibilidad por parte del layout
    bool want = false;
};

void registerWidgetClasses(HINSTANCE inst);
Widget* createWidget(HWND parent, WType type, int id, const wchar_t* text);
Widget* findWidget(int id);
const std::vector<std::unique_ptr<Widget>>& allWidgets();
void invalidateWidget(Widget* w);
