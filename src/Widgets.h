// Controles propios con tema oscuro (MFC): boton, interruptor, slider, rueda de color y desplegable.
// Cada control es un CVLWidget (derivado de CWnd) con su propio mapa de mensajes; avisa a la
// ventana padre con el mensaje WM_WIDGET (wParam = id del control, lParam = WEvent).
#pragma once
#include "Util.h"

#include <memory>
#include <string>
#include <vector>

#define WM_WIDGET (WM_APP + 10)

enum WEvent { EV_BEGIN = 1, EV_CHANGE, EV_END, EV_CLICK };
enum WType { WT_BUTTON, WT_TOGGLE, WT_SLIDER, WT_WHEEL, WT_DROP };
enum WIcon { IC_NONE, IC_PLAY, IC_PAUSE, IC_PREV, IC_NEXT };

class CVLWidget : public CWnd {
public:
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
    CBitmap wheelBmp;
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

    BOOL CreateWidget(CWnd* parent);
    void notify(int ev);                // envia WM_WIDGET al padre

protected:
    afx_msg BOOL OnEraseBkgnd(CDC*) { return TRUE; }
    afx_msg void OnPaint();
    afx_msg void OnMouseMove(UINT flags, CPoint pt);
    afx_msg void OnMouseLeave();
    afx_msg void OnLButtonDown(UINT flags, CPoint pt)   { mouseDown(pt, false); }
    afx_msg void OnLButtonDblClk(UINT flags, CPoint pt) { mouseDown(pt, true); }
    afx_msg void OnLButtonUp(UINT flags, CPoint pt);
    afx_msg void OnCaptureChanged(CWnd* wnd);
    afx_msg void OnEnable(BOOL enable) { Invalidate(FALSE); }
    DECLARE_MESSAGE_MAP()

private:
    void mouseDown(CPoint pt, bool dbl);
    void sliderMouse(int x);
    void wheelMouse(int x, int y);
    void paintButton(CDC* dc, const RECT& rc, bool enabled);
    void paintToggle(CDC* dc, const RECT& rc, bool enabled);
    void paintSlider(CDC* dc, const RECT& rc, bool enabled);
    void paintDrop(CDC* dc, const RECT& rc, bool enabled);
    void paintWheel(CDC* dc, const RECT& rc, bool enabled);
    void openPopup();
};

using Widget = CVLWidget;

CVLWidget* createWidget(CWnd* parent, WType type, int id, const wchar_t* text);
CVLWidget* findWidget(int id);
const std::vector<std::unique_ptr<CVLWidget>>& allWidgets();
void invalidateWidget(CVLWidget* w);
