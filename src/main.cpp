// VISTA + CONTROLADOR (MVC) del editor, con interfaz de paginas al estilo DaVinci Resolve.
// Modelo: Project / MediaSource / Renderer / Exporter / Scopes.
#include "Util.h"

#include <dwmapi.h>
#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>
#include <vector>

extern "C" {
#include <libavutil/avutil.h>   // av_version_info(): version de FFmpeg que se enlazo
}

#include "AudioEngine.h"
#include "AudioMix.h"
#include "AudioSource.h"
#include "Exporter.h"
#include "MediaSource.h"
#include "Project.h"
#include "Renderer.h"
#include "Scopes.h"
#include "Theme.h"
#include "Widgets.h"

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "winmm.lib")

// ---------------- Constantes ----------------
enum Page { PG_MEDIA = 0, PG_EDIT, PG_COLOR, PG_DELIVER };
static const wchar_t* kPageNames[] = { L"Medios", L"Edición", L"Color", L"Entrega" };

enum {
    CID_IMPORT = 1, CID_ADDCLIP, CID_PLAY, CID_HOME, CID_END,
    CID_SPLIT, CID_DELETE, CID_ADDTEXT, CID_UNDO, CID_REDO, CID_TLZOOM,
    CID_SPEED, CID_ZOOM, CID_POSX, CID_POSY, CID_BRIGHT, CID_CONTRAST, CID_SAT, CID_TEMP, CID_TINT,
    CID_FILTER, CID_TRANS, CID_TRANSDUR, CID_RESETFX, CID_RESETGRADE,
    CID_TXT_EDIT, CID_TXT_SIZE, CID_TXT_X, CID_TXT_Y, CID_TXT_COLOR, CID_TXT_BOX,
    CID_WH_LIFT, CID_WH_GAMMA, CID_WH_GAIN, CID_SCOPE,
    CID_RES, CID_FPS, CID_QUALITY, CID_BROWSE, CID_RENDER,
    CID_ADDAUDIO, CID_A_VOL, CID_A_FIN, CID_A_FOUT, CID_A_MUTE,
    CID_MAX
};
static const int CID_TIMER = 1;
#define WM_EXPORT_PROGRESS (WM_APP + 1)
#define WM_EXPORT_DONE     (WM_APP + 2)

static const int TITLE_H = 32, PAGEBAR_H = 46, TOOLS_H = 40, RULER_H = 26, TRACK1_H = 62, TRACK2_H = 30, TRACK3_H = 38;
static const int EDGE = 7, TL_PAD = 12, TL_HDR = 52, POOL_W = 290, INSP_W = 330, HEAD_H = 28;
static const int CELL_W = 134, CELL_H = 104, THUMB_W = 192, THUMB_H = 108;

// ---------------- Estado ----------------
static Project  g_project;
static Renderer g_renderer;
static AudioBank   g_bank;      // audio decodificado, por indice de medio
static AudioEngine g_engine;    // reproduccion con waveOut
static Canvas   g_canvas, g_srcCanvas, g_scopeCanvas;
static std::vector<std::unique_ptr<Canvas>> g_thumbs;

static int    g_page = PG_EDIT;
static double g_playhead = 0.0;
static bool   g_playing = false;
static double g_playStartT = 0.0;
static ULONGLONG g_playStartTick = 0;
static int    g_selClip = -1, g_selText = -1, g_selMedia = -1, g_selAudio = -1;
static double g_pps = 80.0;
static int    g_scrollX = 0, g_poolScroll = 0;
static int    g_scopeMode = SC_WAVE;
static bool   g_syncing = false, g_editUndoPending = false;
static std::wstring g_msg = L"Importa un video o un audio para comenzar.";

static std::atomic<bool> g_cancel{ false };
static bool         g_exporting = false;
static int          g_exportPct = 0;
static std::thread  g_worker;
static std::string  g_exportErr;
static std::wstring g_outPath;
static bool         g_lastExportOk = false;

static CEdit  g_editCtl;      // control MFC del inspector (texto del titulo)
static CBrush g_editBrush;     // fondo del CEdit con el tema oscuro
static Widget* g_w[CID_MAX] = {};
static Widget* wd(int id) { return g_w[id]; }

static RECT g_rcPool, g_rcViewer, g_rcViewImg, g_rcTransport, g_rcInsp, g_rcTools, g_rcTimeline;
static RECT g_rcScope, g_rcColor, g_rcSettings, g_rcQueue, g_rcMeta, g_rcPageBar, g_rcTitle;
static bool g_tlShowText = true;
static RECT g_paintRc;

struct Section { int y; std::wstring title; };
static std::vector<Section> g_secs;

static const double kSpeeds[] = { 0.25, 0.5, 0.75, 1.0, 1.5, 2.0, 3.0, 4.0 };
static const double kTransDurs[] = { 0.5, 1.0, 2.0 };
struct ColorEntry { const wchar_t* name; uint32_t rgb; };
static const ColorEntry kColors[] = {
    { L"Blanco", 0xFFFFFF }, { L"Amarillo", 0xFFD400 }, { L"Rojo", 0xFF3B30 },
    { L"Verde", 0x34C759 }, { L"Azul", 0x0A84FF }, { L"Negro", 0x000000 } };
static const COLORREF kClipColors[] = {
    RGB(58, 110, 165), RGB(150, 86, 78), RGB(78, 135, 92), RGB(152, 124, 58), RGB(112, 86, 150), RGB(52, 132, 140) };

static void afterEdit(CWnd* wnd);
static void renderPreview(CWnd* wnd);
static void layoutAll(CWnd* wnd);
static void syncInspector();
static void startAudio(CWnd* wnd);

// ---------------- Utilidades ----------------
static std::string toUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}
static std::wstring toWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}
static std::wstring fmtTime(double s) {
    if (s < 0) s = 0;
    int t = (int)s;
    wchar_t b[32];
    wfmt(b, L"%02d:%02d", t / 60, t % 60);
    return b;
}
static std::wstring fmtTC(double s) {
    if (s < 0) s = 0;
    int fps = std::max(1, (int)std::lround(g_project.fps));
    long long fr = (long long)std::floor(s * fps + 0.0001);
    int ff = (int)(fr % fps);
    long long ts = fr / fps;
    wchar_t b[48];
    wfmt(b, L"%02d:%02d:%02d:%02d", (int)(ts / 3600), (int)((ts / 60) % 60), (int)(ts % 60), ff);
    return b;
}
static int msgBox(CWnd* owner, const wchar_t* text, const wchar_t* title, UINT flags) {
    CWnd* w = owner ? owner : AfxGetMainWnd();
    return w ? w->MessageBox(text, title, flags) : AfxMessageBox(text, flags);
}
// 'filter' usa el formato Win32 "Descripcion\0*.ext\0\0"; se convierte al formato de MFC "Desc|*.ext||".
static bool pickFile(CWnd* owner, bool save, const wchar_t* filter, const wchar_t* defExt, std::wstring& out) {
    CString flt;
    for (const wchar_t* p = filter; *p; p += wcslen(p) + 1) { flt += p; flt += L'|'; }
    flt += L'|';
    DWORD flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    CFileDialog dlg(save ? FALSE : TRUE, defExt, nullptr, flags, flt, owner);
    if (dlg.DoModal() != IDOK) return false;
    out = dlg.GetPathName().GetString();
    return true;
}
static Clip* selClip() {
    return (g_selClip >= 0 && g_selClip < (int)g_project.clips.size()) ? &g_project.clips[g_selClip] : nullptr;
}
static TextItem* selText() {
    return (g_selText >= 0 && g_selText < (int)g_project.texts.size()) ? &g_project.texts[g_selText] : nullptr;
}
static AudioClip* selAudio() {
    return (g_selAudio >= 0 && g_selAudio < (int)g_project.audio.size()) ? &g_project.audio[g_selAudio] : nullptr;
}
static bool inv(const RECT& r) { RECT t; return IntersectRect(&t, &r, &g_paintRc) != 0; }
static int rw(const RECT& r) { return r.right - r.left; }
static int rh(const RECT& r) { return r.bottom - r.top; }

// ---------------- Deshacer / rehacer ----------------
struct Snapshot { std::vector<Clip> clips; std::vector<TextItem> texts; std::vector<AudioClip> audio; int selClip, selText, selAudio; };
static std::vector<Snapshot> g_undo, g_redo;

static Snapshot takeSnapshot() { return { g_project.clips, g_project.texts, g_project.audio, g_selClip, g_selText, g_selAudio }; }
static void pushUndo() {
    g_undo.push_back(takeSnapshot());
    if (g_undo.size() > 100) g_undo.erase(g_undo.begin());
    g_redo.clear();
}
static void restoreSnapshot(const Snapshot& s) {
    g_project.clips = s.clips;
    g_project.texts = s.texts;
    g_project.audio = s.audio;
    g_selClip = s.selClip;
    g_selText = s.selText;
    g_selAudio = s.selAudio;
}
static void doUndo(CWnd* wnd) {
    if (g_undo.empty()) return;
    g_redo.push_back(takeSnapshot());
    restoreSnapshot(g_undo.back());
    g_undo.pop_back();
    afterEdit(wnd);
}
static void doRedo(CWnd* wnd) {
    if (g_redo.empty()) return;
    g_undo.push_back(takeSnapshot());
    restoreSnapshot(g_redo.back());
    g_redo.pop_back();
    afterEdit(wnd);
}

// ---------------- Linea de tiempo: geometria ----------------
static int track1Top() { return g_rcTimeline.top + RULER_H + 8; }
static int track2Top() { return track1Top() + TRACK1_H + 8; }
static int track3Top() { return track2Top() + TRACK2_H + 8; }   // A1
static int track4Top() { return track3Top() + TRACK3_H + 8; }   // A2
static int timeToX(double t) { return g_rcTimeline.left + TL_HDR + TL_PAD + (int)(t * g_pps) - g_scrollX; }
static double xToTime(int x) { return (x - g_rcTimeline.left - TL_HDR - TL_PAD + g_scrollX) / g_pps; }
static void clampScroll() {
    int viewW = rw(g_rcTimeline) - TL_HDR;
    int contentW = TL_PAD + (int)(g_project.duration() * g_pps) + 300;
    g_scrollX = std::clamp(g_scrollX, 0, std::max(0, contentW - viewW));
}
static void followPlayhead() {
    int viewW = rw(g_rcTimeline) - TL_HDR;
    int x = timeToX(g_playhead) - g_rcTimeline.left - TL_HDR;
    if (x < 20 || x > viewW - 40) {
        g_scrollX = (int)(g_playhead * g_pps) - viewW / 5;
        clampScroll();
    }
}

enum HitType { H_NONE, H_RULER, H_CLIP, H_CLIP_L, H_CLIP_R, H_TEXT, H_TEXT_L, H_TEXT_R, H_AUDIO, H_AUDIO_L, H_AUDIO_R };
struct Hit { int type = H_NONE; int idx = -1; };

static Hit hitTest(int mx, int my) {
    Hit h;
    const RECT& r = g_rcTimeline;
    if (mx < r.left + TL_HDR || mx >= r.right || my < r.top || my >= r.bottom) return h;
    if (my < r.top + RULER_H) { h.type = H_RULER; return h; }

    int y1 = track1Top(), y2 = track2Top(), y3 = track3Top(), y4 = track4Top();
    if (g_tlShowText && ((my >= y3 && my < y3 + TRACK3_H) || (my >= y4 && my < y4 + TRACK3_H))) {
        int tr = (my >= y4) ? 1 : 0;
        for (int i = (int)g_project.audio.size() - 1; i >= 0; --i) {
            const AudioClip& a = g_project.audio[i];
            if (a.track != tr) continue;
            int x0 = timeToX(a.start), x1 = timeToX(a.end());
            if (mx >= x0 && mx < x1) {
                h.idx = i;
                h.type = (mx < x0 + EDGE) ? H_AUDIO_L : (mx >= x1 - EDGE ? H_AUDIO_R : H_AUDIO);
                return h;
            }
        }
        return h;
    }
    if (my >= y1 && my < y1 + TRACK1_H) {
        double s = 0;
        for (int i = 0; i < (int)g_project.clips.size(); ++i) {
            double d = g_project.clips[i].duration();
            int x0 = timeToX(s), x1 = timeToX(s + d);
            if (mx >= x0 && mx < x1) {
                h.idx = i;
                h.type = (mx < x0 + EDGE) ? H_CLIP_L : (mx >= x1 - EDGE ? H_CLIP_R : H_CLIP);
                return h;
            }
            s += d;
        }
    } else if (g_tlShowText && my >= y2 && my < y2 + TRACK2_H) {
        for (int i = (int)g_project.texts.size() - 1; i >= 0; --i) {
            const TextItem& t = g_project.texts[i];
            int x0 = timeToX(t.start), x1 = timeToX(t.start + t.dur);
            if (mx >= x0 && mx < x1) {
                h.idx = i;
                h.type = (mx < x0 + EDGE) ? H_TEXT_L : (mx >= x1 - EDGE ? H_TEXT_R : H_TEXT);
                return h;
            }
        }
    }
    return h;
}

// ---------------- Invalidacion y vista previa ----------------
static void invalidateTimeline(CWnd* wnd) { wnd->InvalidateRect(&g_rcTimeline, FALSE); }
static void invalidateBar(CWnd* wnd) { wnd->InvalidateRect(&g_rcPageBar, FALSE); }

// Dibuja la forma de onda completa del audio en un lienzo (para medios que solo tienen audio).
static void makeWaveImage(const AudioSource& as, Canvas& cv) {
    if (!cv.bits || cv.w <= 0 || cv.h <= 0) return;
    GdiFlush();
    const int W = cv.w, H = cv.h;
    for (size_t i = 0; i < (size_t)W * H; ++i) {
        uint8_t* p = cv.bits + i * 4;
        p[0] = 30; p[1] = 26; p[2] = 22; p[3] = 255;   // fondo (BGRA)
    }
    const auto& pk = as.peaks();
    const int cy = H / 2;
    for (int x = 0; x < W && !pk.empty(); ++x) {
        size_t a = (size_t)((int64_t)x * (int64_t)pk.size() / W);
        size_t b = std::max(a + 1, (size_t)((int64_t)(x + 1) * (int64_t)pk.size() / W));
        int m = 0;
        for (size_t k = a; k < b && k < pk.size(); ++k) m = std::max(m, (int)pk[k]);
        int hh = std::max(1, (int)((H / 2 - 4) * (m / 255.0)));
        for (int y = cy - hh; y <= cy + hh; ++y) {
            uint8_t* p = cv.bits + ((size_t)y * W + x) * 4;
            p[0] = 170; p[1] = 220; p[2] = 90; p[3] = 255;   // verde azulado
        }
    }
}

static void refreshSourcePreview() {
    if (g_selMedia < 0 || g_selMedia >= (int)g_project.media.size() || !g_srcCanvas.bits) return;
    if (!g_project.media[g_selMedia].hasVideo) {
        if (const AudioSource* as = g_bank.get(g_selMedia)) makeWaveImage(*as, g_srcCanvas);
        return;
    }
    Project tp;
    tp.media.push_back(g_project.media[g_selMedia]);
    Clip c;
    c.media = 0;
    c.srcIn = 0.0;
    c.srcOut = tp.media[0].duration;
    tp.clips.push_back(c);
    Renderer r;
    r.render(tp, std::min(0.5, tp.media[0].duration * 0.5), g_srcCanvas);
}

static void renderPreview(CWnd* wnd) {
    g_renderer.render(g_project, g_playhead, g_canvas);
    if (g_page == PG_COLOR) {
        renderScope(g_scopeMode, g_canvas.bits, g_canvas.w, g_canvas.h, g_scopeCanvas);
        wnd->InvalidateRect(&g_rcScope, FALSE);
    }
    wnd->InvalidateRect(&g_rcViewer, FALSE);
}

static void setPlayhead(CWnd* wnd, double t) {
    g_playhead = std::clamp(t, 0.0, g_project.duration());
    if (g_playing) {
        g_playStartT = g_playhead;
        g_playStartTick = GetTickCount64();
        if (g_engine.running()) g_engine.restart(g_playhead);
    }
    renderPreview(wnd);
    invalidateTimeline(wnd);
}

// Arranca / reinicia / detiene el audio segun el proyecto (el reloj de audio manda mientras suena).
static void startAudio(CWnd* wnd) {
    (void)wnd;
    if (!g_project.hasAudio()) {
        if (g_engine.running()) {   // se quedo sin audio: volver al reloj normal
            g_engine.stop();
            g_playStartT = g_playhead;
            g_playStartTick = GetTickCount64();
        }
        return;
    }
    if (g_engine.running()) g_engine.restart(g_playhead);
    else if (!g_engine.start(&g_project, &g_bank, g_playhead)) g_msg = L"No se pudo abrir el dispositivo de audio.";
}

static void stopPlay(CWnd* wnd) {
    g_engine.stop();
    g_playing = false;
    wnd->KillTimer(CID_TIMER);
    wd(CID_PLAY)->icon = IC_PLAY;
    invalidateWidget(wd(CID_PLAY));
}
static void togglePlay(CWnd* wnd) {
    if (g_playing) { stopPlay(wnd); return; }
    double dur = g_project.duration();
    if (dur <= 0.0) return;
    if (g_playhead >= dur - 0.01) g_playhead = 0.0;
    g_playing = true;
    g_playStartT = g_playhead;
    g_playStartTick = GetTickCount64();
    startAudio(wnd);
    wnd->SetTimer(CID_TIMER, 15, nullptr);
    wd(CID_PLAY)->icon = IC_PAUSE;
    invalidateWidget(wd(CID_PLAY));
}

static void jumpEdit(CWnd* wnd, int dir) { // salto al punto de edicion anterior / siguiente
    std::vector<double> pts{ 0.0 };
    double s = 0;
    for (const auto& c : g_project.clips) { s += c.duration(); pts.push_back(s); }
    for (const auto& a : g_project.audio) { pts.push_back(a.start); pts.push_back(a.end()); }
    std::sort(pts.begin(), pts.end());
    const double eps = 0.02;
    if (dir > 0) {
        for (double p : pts) if (p > g_playhead + eps) { setPlayhead(wnd, p); followPlayhead(); return; }
    } else {
        for (int i = (int)pts.size() - 1; i >= 0; --i)
            if (pts[i] < g_playhead - eps) { setPlayhead(wnd, pts[i]); followPlayhead(); return; }
    }
}

// ---------------- Operaciones de edicion ----------------
static void addClip(CWnd* wnd, int mediaIdx) {
    if (mediaIdx < 0 || mediaIdx >= (int)g_project.media.size()) return;
    if (!g_project.media[mediaIdx].hasVideo) return;
    pushUndo();
    Clip c;
    c.media = mediaIdx;
    c.srcIn = 0.0;
    c.srcOut = g_project.media[mediaIdx].duration;
    double start = g_project.duration();
    g_project.clips.push_back(c);
    g_selClip = (int)g_project.clips.size() - 1;
    g_selText = g_selAudio = -1;
    g_playhead = start;
    g_msg = L"Clip agregado a la línea de tiempo.";
    followPlayhead();
    afterEdit(wnd);
}

// Agrega el audio de un medio a la pista A1 (o A2 si A1 esta ocupada) en la posicion del cabezal.
static void addAudio(CWnd* wnd, int mediaIdx) {
    if (mediaIdx < 0 || mediaIdx >= (int)g_project.media.size() || !g_bank.get(mediaIdx)) return;
    pushUndo();
    AudioClip a;
    a.media = mediaIdx;
    a.srcIn = 0.0;
    a.srcOut = g_project.media[mediaIdx].audioDur;
    a.start = g_playhead;
    auto busy = [&](int tr) {
        for (const auto& o : g_project.audio)
            if (o.track == tr && o.start < a.end() - 1e-6 && o.end() > a.start + 1e-6) return true;
        return false;
    };
    a.track = !busy(0) ? 0 : (!busy(1) ? 1 : 0);
    g_project.audio.push_back(a);
    g_selAudio = (int)g_project.audio.size() - 1;
    g_selClip = g_selText = -1;
    g_msg = L"Audio agregado a la pista A" + std::wstring(a.track == 0 ? L"1" : L"2") + L".";
    afterEdit(wnd);
}

static bool importFile(CWnd* wnd, const std::wstring& path) {
    const std::string u8 = toUtf8(path);
    std::string err;
    StreamInfo si;
    if (!probeStreams(u8, si, err) || (!si.hasVideo && !si.hasAudio)) {
        if (err.empty()) err = "El archivo no contiene video ni audio.";
        msgBox(wnd, (toWide(err) + L"\n" + path).c_str(), L"VideoLab", MB_ICONERROR);
        return false;
    }

    MediaInfo mi;
    mi.path = u8;
    size_t slash = path.find_last_of(L"\\/");
    mi.name = (slash == std::wstring::npos) ? path : path.substr(slash + 1);

    std::shared_ptr<AudioSource> as;
    if (si.hasAudio) {
        as = std::make_shared<AudioSource>();
        HCURSOR oldCur = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
        bool ok = as->load(u8, err);
        SetCursor(oldCur);
        if (!ok) {
            if (!si.hasVideo) {
                msgBox(wnd, (toWide(err) + L"\n" + path).c_str(), L"VideoLab", MB_ICONERROR);
                return false;
            }
            as.reset();   // video cuyo audio no se pudo leer: se usa solo el video
        }
    }

    auto thumb = std::make_unique<Canvas>();
    if (si.hasVideo) {
        MediaSource ms;
        if (!ms.open(u8, err)) {
            msgBox(wnd, (toWide(err) + L"\n" + path).c_str(), L"VideoLab", MB_ICONERROR);
            return false;
        }
        mi.hasVideo = true;
        mi.duration = ms.duration();
        mi.width = ms.width();
        mi.height = ms.height();
        mi.fps = ms.fps();
        if (thumb->create(THUMB_W, THUMB_H)) makeThumbnail(ms, std::min(1.0, mi.duration * 0.1), *thumb);
        ms.close();
    } else {
        mi.hasVideo = false;
        mi.duration = as->duration();
        mi.width = mi.height = 0;
        mi.fps = 0.0;
        if (thumb->create(THUMB_W, THUMB_H)) makeWaveImage(*as, *thumb);
    }
    if (as) {
        mi.hasAudio = true;
        mi.audioDur = as->duration();
    }

    g_project.media.push_back(mi);
    g_thumbs.push_back(std::move(thumb));
    g_bank.src.push_back(as);
    const int idx = (int)g_project.media.size() - 1;
    g_selMedia = idx;
    g_msg = L"Importado: " + mi.name;
    if (g_page == PG_MEDIA) refreshSourcePreview();
    if (!mi.hasVideo) addAudio(wnd, idx);                       // un audio suelto va directo a la pista A1/A2
    else if (g_project.clips.empty()) addClip(wnd, idx);
    else afterEdit(wnd);
    return true;
}

static void doImport(CWnd* wnd) {
    std::wstring path;
    if (pickFile(wnd, false,
                 L"Videos y audio\0*.mp4;*.avi;*.mkv;*.mov;*.webm;*.wmv;*.m4v;*.mp3;*.wav;*.aac;*.flac;*.ogg;*.m4a;*.wma;*.opus\0"
                 L"Videos\0*.mp4;*.avi;*.mkv;*.mov;*.webm;*.wmv;*.m4v\0"
                 L"Audio\0*.mp3;*.wav;*.aac;*.flac;*.ogg;*.m4a;*.wma;*.opus\0"
                 L"Todos los archivos\0*.*\0",
                 nullptr, path))
        importFile(wnd, path);
}

static void doSplit(CWnd* wnd) {
    if (selAudio()) {   // dividir el clip de audio seleccionado
        int i = g_selAudio;
        AudioClip a = g_project.audio[i];
        double local = g_playhead - a.start;
        if (local < 0.1 || a.duration() - local < 0.1) {
            g_msg = L"Coloca el cabezal dentro del clip de audio para dividirlo.";
            invalidateBar(wnd);
            return;
        }
        pushUndo();
        double cut = a.srcIn + local;
        g_project.audio[i].srcOut = cut;
        g_project.audio[i].fadeOut = 0.0;
        AudioClip b = a;
        b.srcIn = cut;
        b.start = a.start + local;
        b.fadeIn = 0.0;
        g_project.audio.insert(g_project.audio.begin() + i + 1, b);
        g_selAudio = i + 1;
        g_msg = L"Audio dividido.";
        afterEdit(wnd);
        return;
    }
    double local = 0.0;
    int i = g_project.clipAt(g_playhead, local);
    if (i < 0) return;
    Clip c = g_project.clips[i];
    if (local < 0.1 || c.duration() - local < 0.1) {
        g_msg = L"Coloca el cabezal dentro de un clip para dividirlo.";
        invalidateBar(wnd);
        return;
    }
    pushUndo();
    double cut = c.srcIn + local * c.speed;
    g_project.clips[i].srcOut = cut;
    Clip b = c;
    b.srcIn = cut;
    b.trans = Trans::None;
    g_project.clips.insert(g_project.clips.begin() + i + 1, b);
    g_selClip = i + 1;
    g_selText = g_selAudio = -1;
    g_msg = L"Clip dividido.";
    afterEdit(wnd);
}

static void doDelete(CWnd* wnd) {
    if (selClip()) {
        pushUndo();
        g_project.clips.erase(g_project.clips.begin() + g_selClip);
        g_selClip = -1;
    } else if (selText()) {
        pushUndo();
        g_project.texts.erase(g_project.texts.begin() + g_selText);
        g_selText = -1;
    } else if (selAudio()) {
        pushUndo();
        g_project.audio.erase(g_project.audio.begin() + g_selAudio);
        g_selAudio = -1;
    } else return;
    afterEdit(wnd);
}

static void doAddText(CWnd* wnd) {
    pushUndo();
    TextItem t;
    t.start = g_playhead;
    g_project.texts.push_back(t);
    g_selText = (int)g_project.texts.size() - 1;
    g_selClip = g_selAudio = -1;
    afterEdit(wnd);
    g_editUndoPending = false;
    g_editCtl.SetFocus();
    g_editCtl.SetSel(0, -1);
}

static void doExport(CWnd* wnd) {
    if (g_exporting) { g_cancel = true; return; }
    if (g_project.duration() <= 0.0) {
        msgBox(wnd, L"Agrega al menos un clip de video o de audio a la línea de tiempo antes de renderizar.", L"VideoLab", MB_ICONINFORMATION);
        return;
    }
    if (g_outPath.empty() && !pickFile(wnd, true, L"Video MP4\0*.mp4\0", L"mp4", g_outPath)) return;
    stopPlay(wnd);

    static const int kRes[3][2] = { { 854, 480 }, { 1280, 720 }, { 1920, 1080 } };
    static const int kFps[4] = { 24, 25, 30, 60 };
    static const double kBpp[3] = { 0.08, 0.15, 0.30 };
    int ri = std::clamp(wd(CID_RES)->sel, 0, 2), fi = std::clamp(wd(CID_FPS)->sel, 0, 3), qi = std::clamp(wd(CID_QUALITY)->sel, 0, 2);
    Project snap = g_project; // copia: se puede seguir editando mientras renderiza
    snap.outW = kRes[ri][0];
    snap.outH = kRes[ri][1];
    snap.fps = kFps[fi];
    snap.bitrateKbps = (int)(snap.outW * (double)snap.outH * snap.fps * kBpp[qi] / 1000.0);

    g_exporting = true;
    g_cancel = false;
    g_exportPct = 0;
    wd(CID_RENDER)->text = L"Cancelar";
    invalidateWidget(wd(CID_RENDER));
    g_msg = L"Renderizando...";
    wnd->InvalidateRect(&g_rcQueue, FALSE);
    invalidateBar(wnd);

    std::string utf8 = toUtf8(g_outPath);
    AudioBank bank = g_bank;   // copia (comparte los buffers de audio, que no cambian)
    HWND target = wnd->GetSafeHwnd();   // el hilo de render solo usa el handle para PostMessage
    g_worker = std::thread([target, snap, bank, utf8]() {
        std::string err;
        bool ok = exportProject(snap, bank, utf8, g_cancel,
                                [target](int pct) { ::PostMessage(target, WM_EXPORT_PROGRESS, (WPARAM)pct, 0); }, err);
        g_exportErr = err;
        ::PostMessage(target, WM_EXPORT_DONE, ok ? 1 : 0, 0);
    });
}

// ---------------- Sincronizacion de controles con la seleccion ----------------
static void setS(int id, double v) { Widget* w = g_w[id]; w->val = v; invalidateWidget(w); }
static void setSel(int id, int s) { Widget* w = g_w[id]; w->sel = s; invalidateWidget(w); }

static void syncInspector() {
    g_syncing = true;
    if (Clip* c = selClip()) {
        int si = 3;
        for (int i = 0; i < 8; ++i) if (std::fabs(kSpeeds[i] - c->speed) < 0.001) si = i;
        setSel(CID_SPEED, si);
        setS(CID_ZOOM, c->zoom);
        setS(CID_POSX, c->posX);
        setS(CID_POSY, c->posY);
        setS(CID_BRIGHT, c->brightness);
        setS(CID_CONTRAST, c->contrast);
        setS(CID_SAT, c->saturation);
        setS(CID_TEMP, c->temp);
        setS(CID_TINT, c->tint);
        setSel(CID_FILTER, (int)c->filter);
        setSel(CID_TRANS, (int)c->trans);
        int ti = 0;
        for (int i = 0; i < 3; ++i) if (std::fabs(kTransDurs[i] - c->transDur) < 0.001) ti = i;
        setSel(CID_TRANSDUR, ti);
        wd(CID_TRANSDUR)->EnableWindow(c->trans != Trans::None);
        const Wheel* ws[3] = { &c->lift, &c->gamma, &c->gain };
        for (int k = 0; k < 3; ++k) {
            Widget* w = g_w[CID_WH_LIFT + k];
            w->wx = ws[k]->x; w->wy = ws[k]->y; w->val = ws[k]->master;
            invalidateWidget(w);
        }
    } else if (AudioClip* a = selAudio()) {
        setS(CID_A_VOL, a->volume);
        setS(CID_A_FIN, a->fadeIn);
        setS(CID_A_FOUT, a->fadeOut);
        wd(CID_A_MUTE)->checked = a->mute;
        invalidateWidget(wd(CID_A_MUTE));
    } else if (TextItem* t = selText()) {
        g_editCtl.SetWindowText(t->text.c_str());
        setS(CID_TXT_SIZE, t->size);
        setS(CID_TXT_X, std::lround(t->x * 100));
        setS(CID_TXT_Y, std::lround(t->y * 100));
        int ci = 0;
        for (int i = 0; i < 6; ++i) if (kColors[i].rgb == t->rgb) ci = i;
        setSel(CID_TXT_COLOR, ci);
        wd(CID_TXT_BOX)->checked = t->box;
        invalidateWidget(wd(CID_TXT_BOX));
    }
    g_syncing = false;
}

static void updateEnable() {
    bool hasDur = g_project.duration() > 0.0;
    bool clip = selClip() != nullptr, text = selText() != nullptr;
    auto en = [](int id, bool on) { g_w[id]->EnableWindow(on ? TRUE : FALSE); };
    en(CID_PLAY, hasDur); en(CID_HOME, hasDur); en(CID_END, hasDur);
    bool aud = selAudio() != nullptr;
    en(CID_SPLIT, !g_project.clips.empty() || aud);
    en(CID_DELETE, clip || text || aud);
    en(CID_UNDO, !g_undo.empty());
    en(CID_REDO, !g_redo.empty());
    bool selOk = g_selMedia >= 0 && g_selMedia < (int)g_project.media.size();
    en(CID_ADDCLIP, selOk && g_project.media[g_selMedia].hasVideo);
    en(CID_ADDAUDIO, selOk && g_project.media[g_selMedia].hasAudio);
    for (int id : { CID_A_VOL, CID_A_FIN, CID_A_FOUT, CID_A_MUTE }) en(id, aud);
    for (int id : { CID_SPEED, CID_ZOOM, CID_POSX, CID_POSY, CID_BRIGHT, CID_CONTRAST, CID_SAT, CID_TEMP, CID_TINT,
                    CID_FILTER, CID_TRANS, CID_RESETFX, CID_RESETGRADE, CID_WH_LIFT, CID_WH_GAMMA, CID_WH_GAIN })
        en(id, clip);
    en(CID_TRANSDUR, clip && selClip()->trans != Trans::None);
}

static void afterEdit(CWnd* wnd) {
    double dur = g_project.duration();
    g_playhead = std::clamp(g_playhead, 0.0, dur);
    if (g_selClip >= (int)g_project.clips.size()) g_selClip = -1;
    if (g_selText >= (int)g_project.texts.size()) g_selText = -1;
    if (g_selAudio >= (int)g_project.audio.size()) g_selAudio = -1;
    if (g_playing && dur <= 0.0) stopPlay(wnd);
    else if (g_playing) startAudio(wnd);
    syncInspector();
    updateEnable();
    layoutAll(wnd);
    clampScroll();
    renderPreview(wnd);
    wnd->InvalidateRect(nullptr, FALSE);
}

// ---------------- Paginas ----------------
static void setPage(CWnd* wnd, int p) {
    if (p == g_page) return;
    g_page = p;
    if (p == PG_COLOR && !selClip() && !g_project.clips.empty()) {
        double local = 0;
        int i = g_project.clipAt(g_playhead, local);
        g_selClip = (i >= 0) ? i : 0;
        g_selText = g_selAudio = -1;
    }
    if (p == PG_MEDIA) refreshSourcePreview();
    syncInspector();
    updateEnable();
    layoutAll(wnd);
    renderPreview(wnd);
    wnd->InvalidateRect(nullptr, FALSE);
}

static RECT tabRect(int i) {
    int tw = 118, total = tw * 4;
    int x0 = (rw(g_rcPageBar) - total) / 2;
    return { x0 + i * tw, g_rcPageBar.top, x0 + (i + 1) * tw, g_rcPageBar.bottom };
}

// ---------------- Layout ----------------
static void place(int id, int x, int y, int w, int h) {
    Widget* wg = g_w[id];
    wg->MoveWindow(x, y, w, h, TRUE);
    wg->want = true;
}

static void computeRects(int W, int H) {
    int cy0 = TITLE_H, cy1 = H - PAGEBAR_H;
    g_rcTitle = { 0, 0, W, TITLE_H };
    g_rcPageBar = { 0, cy1, W, H };
    g_rcPool = g_rcViewer = g_rcInsp = g_rcTools = g_rcTimeline = { 0, 0, 0, 0 };
    g_rcScope = g_rcColor = g_rcSettings = g_rcQueue = g_rcMeta = { 0, 0, 0, 0 };
    g_tlShowText = false;

    if (g_page == PG_EDIT) {
        int tlH = std::clamp((cy1 - cy0) * 40 / 100, 272, 340);
        int topB = cy1 - tlH;
        g_rcPool = { 0, cy0, POOL_W, topB };
        g_rcInsp = { W - INSP_W, cy0, W, topB };
        g_rcViewer = { POOL_W, cy0, W - INSP_W, topB };
        g_rcTools = { 0, topB, W, topB + TOOLS_H };
        g_rcTimeline = { 0, topB + TOOLS_H, W, cy1 };
        g_tlShowText = true;
    } else if (g_page == PG_MEDIA) {
        int vw = 560;
        int vh = HEAD_H + 2 + (vw - 16) * 9 / 16 + 8 + 4;
        g_rcPool = { 0, cy0, W - vw, cy1 };
        g_rcViewer = { W - vw, cy0, W, std::min(cy1, cy0 + vh) };
        g_rcMeta = { W - vw, g_rcViewer.bottom, W, cy1 };
    } else if (g_page == PG_COLOR) {
        int tlH = 118, colorH = 292;
        int topB = cy1 - tlH - colorH;
        int scopeW = std::clamp(W * 34 / 100, 380, 520);
        g_rcViewer = { 0, cy0, W - scopeW, topB };
        g_rcScope = { W - scopeW, cy0, W, topB };
        g_rcColor = { 0, topB, W, topB + colorH };
        g_rcTimeline = { 0, topB + colorH, W, cy1 };
    } else {
        int tlH = 118, topB = cy1 - tlH;
        g_rcSettings = { 0, cy0, 330, topB };
        g_rcQueue = { W - 330, cy0, W, topB };
        g_rcViewer = { 330, cy0, W - 330, topB };
        g_rcTimeline = { 0, topB, W, cy1 };
    }

    if (rw(g_rcViewer) > 0) {
        g_rcViewImg = { g_rcViewer.left + 10, g_rcViewer.top + HEAD_H + 4, g_rcViewer.right - 10,
                        g_rcViewer.bottom - 54 };
        g_rcTransport = { g_rcViewer.left, g_rcViewer.bottom - 50, g_rcViewer.right, g_rcViewer.bottom };
        if (g_page == PG_MEDIA) {
            g_rcViewImg.bottom = g_rcViewer.bottom - 6;
            g_rcTransport = { 0, 0, 0, 0 };
        }
    }
}

static void layoutTransport() {
    if (rw(g_rcTransport) <= 0) return;
    int cx = (g_rcTransport.left + g_rcTransport.right) / 2, y = g_rcTransport.top + 9;
    place(CID_HOME, cx - 80, y, 44, 32);
    place(CID_PLAY, cx - 28, y, 56, 32);
    place(CID_END, cx + 36, y, 44, 32);
}

static void section(int& y, const wchar_t* title) {
    g_secs.push_back({ y, title });
    y += 24;
}

static void layoutAll(CWnd* wnd) {
    RECT rc;
    wnd->GetClientRect(&rc);
    int W = rc.right, H = rc.bottom;
    if (W <= 0 || H <= 0) return;
    computeRects(W, H);
    g_secs.clear();
    for (auto& w : allWidgets()) w->want = false;
    bool wantEdit = false;

    // --- Pool (Medios / Edicion)
    if (rw(g_rcPool) > 0) {
        place(CID_IMPORT, g_rcPool.right - 112, g_rcPool.top + 3, 104, 24);
        int bw = (rw(g_rcPool) - 24 - 8) / 2;
        place(CID_ADDCLIP, g_rcPool.left + 12, g_rcPool.bottom - 44, bw, 32);
        place(CID_ADDAUDIO, g_rcPool.left + 12 + bw + 8, g_rcPool.bottom - 44, bw, 32);
    }
    layoutTransport();

    // --- Edicion
    if (g_page == PG_EDIT) {
        int x = 12, y = g_rcTools.top + 6;
        auto put = [&](int id, int w) { place(id, x, y, w, 28); x += w + 8; };
        put(CID_SPLIT, 90); put(CID_DELETE, 90); put(CID_ADDTEXT, 90); put(CID_UNDO, 90); put(CID_REDO, 90);
        place(CID_TLZOOM, W - 12 - 170, g_rcTools.top + 6, 170, 28);

        int ix = g_rcInsp.left + 14, iw = rw(g_rcInsp) - 28, iy = g_rcInsp.top + HEAD_H + 10;
        auto row = [&](int id) { place(id, ix, iy, iw, 26); iy += 28; };
        if (selClip()) {
            section(iy, L"VIDEO");
            row(CID_SPEED); row(CID_ZOOM); row(CID_POSX); row(CID_POSY);
            iy += 4; section(iy, L"COLOR");
            row(CID_BRIGHT); row(CID_CONTRAST); row(CID_SAT);
            iy += 4; section(iy, L"EFECTOS");
            row(CID_FILTER); row(CID_TRANS); row(CID_TRANSDUR);
            iy += 6;
            place(CID_RESETFX, ix, iy, iw, 28);
        } else if (selAudio()) {
            section(iy, L"AUDIO");
            row(CID_A_VOL);
            iy += 4; section(iy, L"FUNDIDOS");
            row(CID_A_FIN); row(CID_A_FOUT);
            iy += 4;
            place(CID_A_MUTE, ix, iy, iw, 26);
        } else if (selText()) {
            section(iy, L"TÍTULO");
            g_editCtl.MoveWindow(ix, iy, iw, 64, TRUE);
            wantEdit = true;
            iy += 74;
            section(iy, L"ESTILO");
            row(CID_TXT_SIZE); row(CID_TXT_X); row(CID_TXT_Y); row(CID_TXT_COLOR);
            place(CID_TXT_BOX, ix, iy, iw, 26);
        }
    }

    // --- Color
    if (g_page == PG_COLOR) {
        place(CID_SCOPE, g_rcScope.right - 176, g_rcScope.top + 2, 168, 24);
        int slW = 360, px = g_rcColor.right - slW - 18;
        int wheelsW = px - g_rcColor.left - 8;
        int ww = wheelsW / 3;
        for (int i = 0; i < 3; ++i) place(CID_WH_LIFT + i, g_rcColor.left + 8 + i * ww, g_rcColor.top + HEAD_H + 6, ww - 8,
                                           rh(g_rcColor) - HEAD_H - 12);
        int iy = g_rcColor.top + HEAD_H + 12;
        section(iy, L"PRIMARIOS");
        for (int id : { CID_CONTRAST, CID_SAT, CID_BRIGHT, CID_TEMP, CID_TINT }) { place(id, px, iy, slW, 26); iy += 30; }
        iy += 6;
        place(CID_RESETGRADE, px, iy, slW, 28);
    }

    // --- Entrega
    if (g_page == PG_DELIVER) {
        int sx = g_rcSettings.left + 16, sw = rw(g_rcSettings) - 32, sy = g_rcSettings.top + HEAD_H + 12;
        section(sy, L"AJUSTES DE VIDEO");
        sy += 24; // linea "Formato"
        place(CID_RES, sx, sy, sw, 26); sy += 30;
        place(CID_FPS, sx, sy, sw, 26); sy += 30;
        place(CID_QUALITY, sx, sy, sw, 26); sy += 40;
        section(sy, L"ARCHIVO DE SALIDA");
        sy += 26; // ruta
        place(CID_BROWSE, sx, sy, sw, 28);
        place(CID_RENDER, sx, g_rcSettings.bottom - 56, sw, 40);
    }

    // mostrar / ocultar segun corresponda
    for (auto& w : allWidgets()) {
        bool vis = w->IsWindowVisible() != FALSE;
        if (w->want != vis) w->ShowWindow(w->want ? SW_SHOWNA : SW_HIDE);
    }
    bool ev = g_editCtl.IsWindowVisible() != FALSE;
    if (wantEdit != ev) g_editCtl.ShowWindow(wantEdit ? SW_SHOWNA : SW_HIDE);
    clampScroll();
}

// ---------------- VISTA: dibujo ----------------
static void panelHeader(CDC* dc, const RECT& r, const wchar_t* title) {
    RECT h{ r.left, r.top, r.right, r.top + HEAD_H };
    fillRc(dc, h, Theme::header);
    lineTo(dc, r.left, h.bottom - 1, r.right, h.bottom - 1, Theme::line);
    textIn(dc, { r.left + 12, h.top, r.right - 8, h.bottom }, title, Theme::text, DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, fBold);
}

static void drawTitleBar(CDC* dc) {
    if (!inv(g_rcTitle)) return;
    fillRc(dc, g_rcTitle, Theme::header);
    lineTo(dc, 0, g_rcTitle.bottom - 1, g_rcTitle.right, g_rcTitle.bottom - 1, Theme::line);
    RECT logo{ 12, 9, 26, 23 };
    roundBox(dc, logo, 4, Theme::accent, Theme::accent);
    textAt(dc, 34, 8, L"VideoLab", Theme::text, fBold);
    RECT mid{ 0, 0, g_rcTitle.right, TITLE_H - 1 };
    textIn(dc, mid, L"Proyecto sin título  •  Editado", Theme::textDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE, fUI);
    wchar_t b[96];
    wfmt(b, L"MFC  •  FFmpeg %S  •  %.0f fps", av_version_info(), g_project.fps);
    textIn(dc, { 0, 0, g_rcTitle.right - 14, TITLE_H - 1 }, b, Theme::textDim, DT_RIGHT | DT_VCENTER | DT_SINGLELINE, fUI);
}

static void drawPageBar(CDC* dc, int W) {
    if (!inv(g_rcPageBar)) return;
    fillRc(dc, g_rcPageBar, Theme::header);
    lineTo(dc, 0, g_rcPageBar.top, W, g_rcPageBar.top, Theme::line);
    textIn(dc, { 14, g_rcPageBar.top, 300, g_rcPageBar.bottom }, L"VideoLab", Theme::textDim, DT_VCENTER | DT_SINGLELINE, fBold);
    for (int i = 0; i < 4; ++i) {
        RECT t = tabRect(i);
        bool act = (i == g_page);
        if (act) {
            RECT bg{ t.left + 4, t.top + 4, t.right - 4, t.bottom - 4 };
            roundBox(dc, bg, 8, Theme::panelHi, Theme::panelHi);
            RECT ul{ t.left + 22, t.bottom - 8, t.right - 22, t.bottom - 5 };
            fillRc(dc, ul, Theme::accent);
        }
        textIn(dc, { t.left, t.top, t.right, t.bottom - 4 }, kPageNames[i], act ? RGB(255, 255, 255) : Theme::textDim,
               DT_CENTER | DT_VCENTER | DT_SINGLELINE, fBold);
    }
    // mensaje / progreso
    std::wstring m = g_msg;
    if (g_exporting) { wchar_t b[64]; wfmt(b, L"Renderizando... %d%%", g_exportPct); m = b; }
    textIn(dc, { W - 520, g_rcPageBar.top, W - 14, g_rcPageBar.bottom }, m, Theme::textDim,
           DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, fUI);
}

static void drawViewer(CDC* dc) {
    if (rw(g_rcViewer) <= 0 || !inv(g_rcViewer)) return;
    fillRc(dc, g_rcViewer, Theme::panel);
    bool src = (g_page == PG_MEDIA);
    panelHeader(dc, g_rcViewer, L"");
    textIn(dc, { g_rcViewer.left, g_rcViewer.top, g_rcViewer.right, g_rcViewer.top + HEAD_H },
           src ? L"Vista previa del medio" : L"Línea de tiempo 1", Theme::textDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE, fUI);
    // imagen con proporcion 16:9
    RECT img = g_rcViewImg;
    fillRc(dc, img, RGB(8, 8, 10));
    int aw = rw(img) - 2, ah = rh(img) - 2;
    Canvas& cv = (src && g_selMedia >= 0 && g_srcCanvas.bits) ? g_srcCanvas : g_canvas;
    if (aw > 20 && ah > 20 && cv.bits && !(src && g_selMedia < 0)) {
        double s = std::min((double)aw / cv.w, (double)ah / cv.h);
        int dw = (int)(cv.w * s), dh = (int)(cv.h * s);
        int dx = img.left + (rw(img) - dw) / 2, dy = img.top + (rh(img) - dh) / 2;
        dc->SetStretchBltMode(HALFTONE);
        dc->SetBrushOrg(0, 0);
        dc->StretchBlt(dx, dy, dw, dh, CDC::FromHandle(cv.dc), 0, 0, cv.w, cv.h, SRCCOPY);
        if (!src && g_project.clips.empty() && g_project.texts.empty() && g_project.audio.empty()) {
            textIn(dc, { dx, dy, dx + dw, dy + dh }, L"Importa un video o un audio para comenzar\n(Ctrl+I o arrastra un archivo aquí)",
                   Theme::textDim, DT_CENTER | DT_VCENTER, fUI);
        }
    } else if (src) {
        textIn(dc, img, L"Selecciona un medio en el panel de la izquierda", Theme::textDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE, fUI);
    }

    // transporte: codigo de tiempo
    if (rw(g_rcTransport) > 0) {
        lineTo(dc, g_rcTransport.left, g_rcTransport.top, g_rcTransport.right, g_rcTransport.top, Theme::line);
        textIn(dc, { g_rcTransport.left + 16, g_rcTransport.top + 2, g_rcTransport.left + 220, g_rcTransport.top + 34 }, fmtTC(g_playhead),
               RGB(236, 236, 240), DT_VCENTER | DT_SINGLELINE, fMono);
        textIn(dc, { g_rcTransport.right - 220, g_rcTransport.top + 2, g_rcTransport.right - 16, g_rcTransport.top + 34 },
               fmtTC(g_project.duration()), Theme::textDim, DT_RIGHT | DT_VCENTER | DT_SINGLELINE, fMono);
        // medidor de nivel de audio (L / R): solo se mueve mientras suena
        float lv[2] = { 0, 0 };
        if (g_playing && g_engine.running()) g_engine.levels(lv[0], lv[1]);
        for (int ch = 0; ch < 2; ++ch) {
            RECT bar{ g_rcTransport.left + 16, g_rcTransport.top + 36 + ch * 6, g_rcTransport.left + 136, g_rcTransport.top + 41 + ch * 6 };
            fillRc(dc, bar, RGB(14, 14, 16));
            double db = lv[ch] > 0.0001f ? 20.0 * std::log10((double)lv[ch]) : -60.0;
            double f = std::clamp((db + 48.0) / 48.0, 0.0, 1.0);
            RECT fl{ bar.left, bar.top, bar.left + (int)(rw(bar) * f), bar.bottom };
            fillRc(dc, fl, db > -3.0 ? RGB(232, 78, 60) : (db > -12.0 ? RGB(230, 190, 60) : RGB(70, 190, 110)));
        }
    }
}

// ---- Pool de medios
static RECT poolGrid() {
    return { g_rcPool.left, g_rcPool.top + HEAD_H, g_rcPool.right, g_rcPool.bottom - 52 };
}
static int poolCols() { return std::max(1, (rw(poolGrid()) - 8) / CELL_W); }
static RECT cellRect(int i) {
    RECT g = poolGrid();
    int cols = poolCols();
    int x = g.left + 8 + (i % cols) * CELL_W;
    int y = g.top + 8 + (i / cols) * CELL_H - g_poolScroll;
    return { x, y, x + CELL_W - 8, y + CELL_H - 8 };
}
static int poolHit(int mx, int my) {
    RECT g = poolGrid();
    POINT pt{ mx, my };
    if (!PtInRect(&g, pt)) return -1;
    for (int i = 0; i < (int)g_project.media.size(); ++i) {
        RECT r = cellRect(i);
        if (PtInRect(&r, pt)) return i;
    }
    return -1;
}
static void clampPoolScroll() {
    RECT g = poolGrid();
    int rows = ((int)g_project.media.size() + poolCols() - 1) / poolCols();
    int content = rows * CELL_H + 16;
    g_poolScroll = std::clamp(g_poolScroll, 0, std::max(0, content - rh(g)));
}

static void drawPool(CDC* dc) {
    if (rw(g_rcPool) <= 0 || !inv(g_rcPool)) return;
    fillRc(dc, g_rcPool, Theme::panel);
    panelHeader(dc, g_rcPool, L"Medios");
    lineTo(dc, g_rcPool.right - 1, g_rcPool.top, g_rcPool.right - 1, g_rcPool.bottom, Theme::line);

    RECT g = poolGrid();
    int saved = dc->SaveDC();
    dc->IntersectClipRect(g.left, g.top, g.right - 1, g.bottom);
    if (g_project.media.empty()) {
        textIn(dc, { g.left + 16, g.top + 30, g.right - 16, g.top + 120 },
               L"No hay medios.\nUsa \"Importar medios\" o arrastra videos y audios a esta ventana.", Theme::textDim, DT_CENTER | DT_WORDBREAK, fUI);
    }
    clampPoolScroll();
    for (int i = 0; i < (int)g_project.media.size(); ++i) {
        RECT r = cellRect(i);
        if (r.bottom < g.top || r.top > g.bottom) continue;
        bool sel = (i == g_selMedia);
        RECT th{ r.left, r.top, r.right, r.top + (rw(r) * 9 / 16) };
        fillRc(dc, th, RGB(10, 10, 12));
        if (i < (int)g_thumbs.size() && g_thumbs[i] && g_thumbs[i]->dc) {
            dc->SetStretchBltMode(HALFTONE);
            dc->SetBrushOrg(0, 0);
            dc->StretchBlt(th.left, th.top, rw(th), rh(th), CDC::FromHandle(g_thumbs[i]->dc), 0, 0, THUMB_W, THUMB_H, SRCCOPY);
        }
        if (sel) {
            CPen sp(PS_SOLID, 2, Theme::accent);
            CPen* op = dc->SelectObject(&sp);
            CGdiObject* ob = dc->SelectStockObject(NULL_BRUSH);
            dc->Rectangle(&th);
            dc->SelectObject(ob);
            dc->SelectObject(op);
        }
        const MediaInfo& mi = g_project.media[i];
        textIn(dc, { r.left, th.bottom + 2, r.right, th.bottom + 18 }, mi.name, sel ? RGB(255, 255, 255) : Theme::text,
               DT_SINGLELINE | DT_END_ELLIPSIS, fSmall);
        if (mi.hasAudio) {   // insignia de audio
            RECT ab{ th.left + 3, th.top + 3, th.left + (mi.hasVideo ? 21 : 52), th.top + 18 };
            fillRc(dc, ab, RGB(0, 0, 0));
            textIn(dc, ab, mi.hasVideo ? L"♪" : L"♪ Audio", RGB(150, 235, 200), DT_CENTER | DT_VCENTER | DT_SINGLELINE, fSmall);
        }
        // duracion sobre la miniatura
        RECT db{ th.right - 40, th.bottom - 16, th.right - 3, th.bottom - 3 };
        fillRc(dc, db, RGB(0, 0, 0));
        textIn(dc, db, fmtTime(mi.duration), RGB(230, 230, 236), DT_CENTER | DT_VCENTER | DT_SINGLELINE, fSmall);
    }
    dc->RestoreDC(saved);
    lineTo(dc, g_rcPool.left, g.bottom, g_rcPool.right, g.bottom, Theme::line);
}

static void drawMeta(CDC* dc) {
    if (rw(g_rcMeta) <= 0 || !inv(g_rcMeta)) return;
    fillRc(dc, g_rcMeta, Theme::panel);
    panelHeader(dc, g_rcMeta, L"Metadatos");
    if (g_selMedia < 0 || g_selMedia >= (int)g_project.media.size()) {
        textIn(dc, { g_rcMeta.left + 16, g_rcMeta.top + HEAD_H + 12, g_rcMeta.right - 16, g_rcMeta.bottom },
               L"Selecciona un medio para ver sus propiedades.", Theme::textDim, DT_WORDBREAK, fUI);
        return;
    }
    const MediaInfo& mi = g_project.media[g_selMedia];
    wchar_t res[48], fps[32], dur[48];
    wfmt(res, L"%d × %d", mi.width, mi.height);
    wfmt(fps, L"%.2f fps", mi.fps);
    wfmt(dur, L"%s  (%.1f s)", fmtTC(mi.duration).c_str(), mi.duration);
    struct Row { const wchar_t* k; std::wstring v; };
    std::vector<Row> rows;
    rows.push_back({ L"Nombre", mi.name });
    rows.push_back({ L"Tipo", mi.hasVideo ? (mi.hasAudio ? L"Video con audio" : L"Video") : L"Audio" });
    if (mi.hasVideo) { rows.push_back({ L"Resolución", res }); rows.push_back({ L"Fotogramas", fps }); }
    rows.push_back({ L"Duración", dur });
    rows.push_back({ L"Audio", mi.hasAudio ? L"44.1 kHz, estéreo (convertido al importar)" : L"Sin audio" });
    rows.push_back({ L"Ruta", toWide(mi.path) });
    int y = g_rcMeta.top + HEAD_H + 10;
    for (auto& r : rows) {
        textAt(dc, g_rcMeta.left + 16, y, r.k, Theme::textDim, fUI);
        textIn(dc, { g_rcMeta.left + 110, y, g_rcMeta.right - 12, y + 18 }, r.v, Theme::text, DT_SINGLELINE | DT_END_ELLIPSIS | DT_PATH_ELLIPSIS, fUI);
        y += 24;
    }
}

// ---- Inspector
static void drawInspector(CDC* dc) {
    if (rw(g_rcInsp) <= 0 || !inv(g_rcInsp)) return;
    fillRc(dc, g_rcInsp, Theme::panel);
    panelHeader(dc, g_rcInsp, L"Inspector");
    lineTo(dc, g_rcInsp.left, g_rcInsp.top, g_rcInsp.left, g_rcInsp.bottom, Theme::line);
    if (Clip* c = selClip()) {
        textIn(dc, { g_rcInsp.right - 200, g_rcInsp.top, g_rcInsp.right - 12, g_rcInsp.top + HEAD_H }, g_project.media[c->media].name,
               Theme::textDim, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, fSmall);
    } else if (AudioClip* a = selAudio()) {
        textIn(dc, { g_rcInsp.right - 200, g_rcInsp.top, g_rcInsp.right - 12, g_rcInsp.top + HEAD_H }, g_project.media[a->media].name,
               Theme::textDim, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, fSmall);
    } else if (!selText()) {
        textIn(dc, { g_rcInsp.left + 18, g_rcInsp.top + HEAD_H + 20, g_rcInsp.right - 18, g_rcInsp.top + 160 },
               L"Selecciona un clip, un audio o un título en la línea de tiempo para editar sus propiedades.", Theme::textDim, DT_WORDBREAK, fUI);
    }
    for (const auto& s : g_secs) {
        int x0 = g_rcInsp.left + 14;
        textAt(dc, x0, s.y + 2, s.title, Theme::textDim, fSmall);
        lineTo(dc, x0 + 90, s.y + 10, g_rcInsp.right - 14, s.y + 10, Theme::line);
    }
}

// ---- Linea de tiempo
static void drawTimeline(CDC* dc) {
    const RECT& r = g_rcTimeline;
    if (rw(r) <= 0 || !inv(r)) return;
    int saved = dc->SaveDC();
    dc->IntersectClipRect(r.left, r.top, r.right, r.bottom);
    fillRc(dc, r, Theme::bg);

    const int x0v = r.left + TL_HDR;
    int y1 = track1Top(), y2 = track2Top(), y3 = track3Top(), y4 = track4Top();

    // carriles
    RECT lane1{ x0v, y1 - 4, r.right, y1 + TRACK1_H + 4 };
    fillRc(dc, lane1, RGB(31, 31, 35));
    if (g_tlShowText) {
        RECT lane2{ x0v, y2 - 4, r.right, y2 + TRACK2_H + 4 };
        fillRc(dc, lane2, RGB(31, 31, 35));
        RECT lane3{ x0v, y3 - 4, r.right, y3 + TRACK3_H + 4 };
        fillRc(dc, lane3, RGB(27, 33, 33));
        RECT lane4{ x0v, y4 - 4, r.right, y4 + TRACK3_H + 4 };
        fillRc(dc, lane4, RGB(27, 33, 33));
    }

    // area desplazable
    dc->SaveDC();
    dc->IntersectClipRect(x0v, r.top, r.right, r.bottom);

    // regla
    RECT ruler{ x0v, r.top, r.right, r.top + RULER_H };
    fillRc(dc, ruler, Theme::header);
    lineTo(dc, x0v, r.top + RULER_H, r.right, r.top + RULER_H, Theme::line);
    static const double steps[] = { 0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300 };
    double step = steps[9];
    for (double s : steps) if (s * g_pps >= 70) { step = s; break; }
    double t0 = xToTime(x0v), t1 = xToTime(r.right);
    for (double t = std::floor(std::max(0.0, t0) / step) * step; t <= t1; t += step) {
        int x = timeToX(t);
        lineTo(dc, x, r.top + RULER_H - 8, x, r.top + RULER_H, Theme::textDim);
        textAt(dc, x + 4, r.top + 5, fmtTime(t), Theme::textDim, fSmall);
        for (int k = 1; k < 4; ++k) { // marcas menores
            int xm = x + (int)(k * step * g_pps / 4);
            lineTo(dc, xm, r.top + RULER_H - 4, xm, r.top + RULER_H, RGB(70, 70, 78));
        }
    }

    if (g_project.clips.empty())
        textAt(dc, x0v + 16, y1 + TRACK1_H / 2 - 8, L"Agrega clips desde \"Medios\" (doble clic o botón)", RGB(90, 90, 104), fUI);
    if (g_tlShowText && g_project.texts.empty())
        textAt(dc, x0v + 16, y2 + TRACK2_H / 2 - 8, L"Usa \"+ Título\" para agregar texto", RGB(90, 90, 104), fUI);
    if (g_tlShowText && g_project.audio.empty())
        textAt(dc, x0v + 16, y3 + TRACK3_H / 2 - 8, L"Importa un audio o usa \"Agregar audio\" en el panel de medios", RGB(90, 90, 104), fUI);

    // clips
    double s = 0;
    for (int i = 0; i < (int)g_project.clips.size(); ++i) {
        const Clip& c = g_project.clips[i];
        double d = c.duration();
        int x0 = timeToX(s), x1 = timeToX(s + d);
        s += d;
        if (x1 < x0v || x0 > r.right) continue;
        bool sel = (i == g_selClip);
        COLORREF base = kClipColors[c.media % 6];
        roundBox(dc, { x0, y1, x1 - 1, y1 + TRACK1_H }, 8, base, sel ? RGB(255, 255, 255) : RGB(12, 12, 14));
        if (sel) roundBox(dc, { x0 + 1, y1 + 1, x1 - 2, y1 + TRACK1_H - 1 }, 7, base, Theme::accent);

        int textX = x0 + 8;
        if (c.media < (int)g_thumbs.size() && g_thumbs[c.media] && x1 - x0 > 110) {
            int th = TRACK1_H - 8, tw = th * 16 / 9;
            dc->SetStretchBltMode(HALFTONE);
            dc->SetBrushOrg(0, 0);
            dc->StretchBlt(x0 + 4, y1 + 4, tw, th, CDC::FromHandle(g_thumbs[c.media]->dc), 0, 0, THUMB_W, THUMB_H, SRCCOPY);
            textX = x0 + 4 + tw + 8;
        }
        RECT tr{ textX, y1 + 8, x1 - 8, y1 + 26 };
        textIn(dc, tr, g_project.media[c.media].name, RGB(255, 255, 255), DT_SINGLELINE | DT_END_ELLIPSIS, fBold);
        std::wstring flags;
        wchar_t b[32];
        wfmt(b, L"%.1f s", d);
        flags = b;
        if (c.speed != 1.0) { wfmt(b, L"  %.2gx", c.speed); flags += b; }
        bool graded = c.brightness || c.contrast || c.saturation || c.temp || c.tint || !c.lift.neutral() || !c.gamma.neutral() || !c.gain.neutral();
        if (graded) flags += L"  ● Color";
        if (c.filter != Filter::None) flags += L"  Fx";
        if (c.zoom != 100 || c.posX || c.posY) flags += L"  ⤢";
        if (c.trans != Trans::None) flags += L"  Fundido";
        textIn(dc, { textX, y1 + 28, x1 - 8, y1 + TRACK1_H - 4 }, flags, RGB(232, 232, 240), DT_SINGLELINE | DT_END_ELLIPSIS, fSmall);

        if (sel) {
            RECT hl{ x0 + 2, y1 + 18, x0 + 5, y1 + TRACK1_H - 18 }, hr{ x1 - 7, y1 + 18, x1 - 4, y1 + TRACK1_H - 18 };
            fillRc(dc, hl, RGB(255, 255, 255));
            fillRc(dc, hr, RGB(255, 255, 255));
        }
    }

    // titulos
    if (g_tlShowText) {
        for (int i = 0; i < (int)g_project.texts.size(); ++i) {
            const TextItem& t = g_project.texts[i];
            int x0 = timeToX(t.start), x1 = timeToX(t.start + t.dur);
            if (x1 < x0v || x0 > r.right) continue;
            bool sel = (i == g_selText);
            roundBox(dc, { x0, y2, x1 - 1, y2 + TRACK2_H }, 8, RGB(118, 82, 168), sel ? Theme::accent : RGB(12, 12, 14));
            textIn(dc, { x0 + 8, y2, x1 - 8, y2 + TRACK2_H }, t.text, RGB(255, 255, 255), DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS, fUI);
        }
    }

    // clips de audio con forma de onda
    if (g_tlShowText) {
        for (int i = 0; i < (int)g_project.audio.size(); ++i) {
            const AudioClip& a = g_project.audio[i];
            int x0 = timeToX(a.start), x1 = timeToX(a.end());
            if (x1 < x0v || x0 > r.right) continue;
            int ya = a.track == 0 ? y3 : y4;
            bool sel = (i == g_selAudio);
            COLORREF base = a.mute ? RGB(64, 68, 70) : RGB(44, 112, 98);
            roundBox(dc, { x0, ya, x1 - 1, ya + TRACK3_H }, 8, base, sel ? RGB(255, 255, 255) : RGB(12, 12, 14));
            if (sel) roundBox(dc, { x0 + 1, ya + 1, x1 - 2, ya + TRACK3_H - 1 }, 7, base, Theme::accent);

            // forma de onda (escalada por el volumen)
            if (const AudioSource* as = g_bank.get(a.media)) {
                const auto& pk = as->peaks();
                CPen wp(PS_SOLID, 1, a.mute ? RGB(120, 126, 128) : RGB(150, 235, 200));
                CPen* owp = dc->SelectObject(&wp);
                const int cy = ya + 24, half = 11;
                const double vol = std::min(1.0, a.volume / 100.0);
                int px0 = std::max(x0 + 3, x0v), px1 = std::min(x1 - 4, (int)r.right);
                for (int px = px0; px < px1; ++px) {
                    double ts = a.srcIn + (px - x0) / g_pps;
                    int64_t w0 = (int64_t)std::floor(ts * 100.0);
                    int64_t w1 = std::max(w0 + 1, (int64_t)std::floor((ts + 1.0 / g_pps) * 100.0));
                    int m = 0;
                    for (int64_t k = std::max<int64_t>(0, w0); k < w1 && k < (int64_t)pk.size(); ++k) m = std::max(m, (int)pk[(size_t)k]);
                    int hh = std::max(0, (int)std::lround(half * vol * (m / 255.0)));
                    dc->MoveTo(px, cy - hh);
                    dc->LineTo(px, cy + hh + 1);
                }
                dc->SelectObject(owp);
            }
            // rampas de fundido
            if (a.fadeIn > 0)  lineTo(dc, x0 + 2, ya + TRACK3_H - 3, x0 + 2 + (int)(a.fadeIn * g_pps), ya + 14, RGB(255, 255, 255));
            if (a.fadeOut > 0) lineTo(dc, x1 - 3, ya + TRACK3_H - 3, x1 - 3 - (int)(a.fadeOut * g_pps), ya + 14, RGB(255, 255, 255));
            std::wstring nm = g_project.media[a.media].name;
            if (a.mute) nm += L"  (silenciado)";
            else if (a.volume != 100) { wchar_t vb[24]; wfmt(vb, L"  %d%%", a.volume); nm += vb; }
            textIn(dc, { x0 + 8, ya + 2, x1 - 8, ya + 16 }, nm, RGB(255, 255, 255), DT_SINGLELINE | DT_END_ELLIPSIS, fSmall);
            if (sel) {
                RECT hl{ x0 + 2, ya + 12, x0 + 5, ya + TRACK3_H - 8 }, hr{ x1 - 7, ya + 12, x1 - 4, ya + TRACK3_H - 8 };
                fillRc(dc, hl, RGB(255, 255, 255));
                fillRc(dc, hr, RGB(255, 255, 255));
            }
        }
    }

    // cabezal
    int px = timeToX(g_playhead);
    lineTo(dc, px, r.top, px, r.bottom, Theme::accent, 2);
    triangle(dc, { px - 7, r.top }, { px + 7, r.top }, { px, r.top + 12 }, Theme::accent);
    dc->RestoreDC(-1);

    // cabeceras de pista (columna izquierda)
    RECT hdr{ r.left, r.top, x0v, r.bottom };
    fillRc(dc, hdr, Theme::panel);
    lineTo(dc, x0v - 1, r.top, x0v - 1, r.bottom, Theme::line);
    textAt(dc, r.left + 10, r.top + 6, L"Pistas", Theme::textDim, fSmall);
    auto trackHdr = [&](int y, int h, const wchar_t* name) {
        RECT b{ r.left + 6, y, x0v - 8, y + h };
        roundBox(dc, b, 6, Theme::btn, Theme::border);
        textIn(dc, b, name, Theme::text, DT_CENTER | DT_VCENTER | DT_SINGLELINE, fBold);
    };
    trackHdr(y1, TRACK1_H, L"V1");
    if (g_tlShowText) {
        trackHdr(y2, TRACK2_H, L"T1");
        trackHdr(y3, TRACK3_H, L"A1");
        trackHdr(y4, TRACK3_H, L"A2");
    }
    dc->RestoreDC(saved);
}

static void drawToolsBar(CDC* dc) {
    if (rw(g_rcTools) <= 0 || !inv(g_rcTools)) return;
    fillRc(dc, g_rcTools, Theme::panel);
    lineTo(dc, 0, g_rcTools.top, g_rcTools.right, g_rcTools.top, Theme::line);
    lineTo(dc, 0, g_rcTools.bottom - 1, g_rcTools.right, g_rcTools.bottom - 1, Theme::line);
    textIn(dc, { g_rcTools.right - 12 - 170 - 50, g_rcTools.top, g_rcTools.right - 12 - 176, g_rcTools.bottom }, L"Zoom",
           Theme::textDim, DT_RIGHT | DT_VCENTER | DT_SINGLELINE, fUI);
}

// ---- Color
static void drawColorPanel(CDC* dc) {
    if (rw(g_rcColor) <= 0 || !inv(g_rcColor)) return;
    fillRc(dc, g_rcColor, Theme::panel);
    panelHeader(dc, g_rcColor, L"Color primario – Ruedas");
    lineTo(dc, 0, g_rcColor.top, g_rcColor.right, g_rcColor.top, Theme::line);
    lineTo(dc, 0, g_rcColor.bottom - 1, g_rcColor.right, g_rcColor.bottom - 1, Theme::line);
    int slW = 360, px = g_rcColor.right - slW - 18;
    lineTo(dc, px - 10, g_rcColor.top + HEAD_H + 8, px - 10, g_rcColor.bottom - 10, Theme::line);
    for (const auto& s : g_secs) {
        textAt(dc, px, s.y + 2, s.title, Theme::textDim, fSmall);
        lineTo(dc, px + 80, s.y + 10, px + slW, s.y + 10, Theme::line);
    }
    if (!selClip()) {
        textIn(dc, { g_rcColor.left + 20, g_rcColor.top + HEAD_H + 80, px - 30, g_rcColor.bottom - 10 },
               L"Selecciona un clip en la línea de tiempo para corregir su color.\n\nDoble clic en una rueda o en su control Y para restablecerla.",
               Theme::textDim, DT_CENTER | DT_WORDBREAK, fUI);
    }
}

static void drawScopePanel(CDC* dc) {
    if (rw(g_rcScope) <= 0 || !inv(g_rcScope)) return;
    fillRc(dc, g_rcScope, Theme::panel);
    panelHeader(dc, g_rcScope, L"Scopes");
    lineTo(dc, g_rcScope.left, g_rcScope.top, g_rcScope.left, g_rcScope.bottom, Theme::line);
    RECT area{ g_rcScope.left + 10, g_rcScope.top + HEAD_H + 8, g_rcScope.right - 10, g_rcScope.bottom - 10 };
    int aw = rw(area), ah = rh(area);
    if (aw < 40 || ah < 40 || !g_scopeCanvas.bits) return;
    double s = std::min((double)aw / g_scopeCanvas.w, (double)ah / g_scopeCanvas.h);
    int dw = (int)(g_scopeCanvas.w * s), dh = (int)(g_scopeCanvas.h * s);
    int dx = area.left + (aw - dw) / 2, dy = area.top + (ah - dh) / 2;
    dc->SetStretchBltMode(HALFTONE);
    dc->SetBrushOrg(0, 0);
    dc->StretchBlt(dx, dy, dw, dh, CDC::FromHandle(g_scopeCanvas.dc), 0, 0, g_scopeCanvas.w, g_scopeCanvas.h, SRCCOPY);
}

// ---- Entrega
static void drawSettings(CDC* dc) {
    if (rw(g_rcSettings) <= 0 || !inv(g_rcSettings)) return;
    fillRc(dc, g_rcSettings, Theme::panel);
    panelHeader(dc, g_rcSettings, L"Ajustes de entrega");
    lineTo(dc, g_rcSettings.right - 1, g_rcSettings.top, g_rcSettings.right - 1, g_rcSettings.bottom, Theme::line);
    int sx = g_rcSettings.left + 16, sw = rw(g_rcSettings) - 32;
    for (const auto& s : g_secs) {
        textAt(dc, sx, s.y + 2, s.title, Theme::textDim, fSmall);
        lineTo(dc, sx + 130, s.y + 10, sx + sw, s.y + 10, Theme::line);
    }
    // fila "Formato" (sin control)
    if (!g_secs.empty()) {
        int y = g_secs[0].y + 24;
        textIn(dc, { sx, y, sx + 110, y + 26 }, L"Formato", Theme::textDim, DT_VCENTER | DT_SINGLELINE, fUI);
        textIn(dc, { sx + 110, y, sx + sw, y + 26 }, L"MP4  (MPEG-4 + audio AAC)", Theme::text, DT_VCENTER | DT_SINGLELINE, fUI);
    }
    if (g_secs.size() > 1) {
        int y = g_secs[1].y + 24;
        RECT pr{ sx, y, sx + sw, y + 22 };
        textIn(dc, pr, g_outPath.empty() ? L"(sin definir)" : g_outPath, g_outPath.empty() ? Theme::textDim : Theme::text,
               DT_VCENTER | DT_SINGLELINE | DT_PATH_ELLIPSIS, fUI);
    }
}

static void drawQueue(CDC* dc) {
    if (rw(g_rcQueue) <= 0 || !inv(g_rcQueue)) return;
    fillRc(dc, g_rcQueue, Theme::panel);
    panelHeader(dc, g_rcQueue, L"Cola de render");
    lineTo(dc, g_rcQueue.left, g_rcQueue.top, g_rcQueue.left, g_rcQueue.bottom, Theme::line);
    RECT card{ g_rcQueue.left + 14, g_rcQueue.top + HEAD_H + 14, g_rcQueue.right - 14, g_rcQueue.top + HEAD_H + 150 };
    if (!g_exporting && g_exportPct == 0 && !g_lastExportOk) {
        textIn(dc, { card.left, card.top, card.right, card.top + 80 },
               L"No hay trabajos.\nConfigura los ajustes y pulsa \"Renderizar\".", Theme::textDim, DT_CENTER | DT_WORDBREAK, fUI);
        return;
    }
    roundBox(dc, card, 8, Theme::field, Theme::border);
    std::wstring name = g_outPath;
    size_t sl = name.find_last_of(L"\\/");
    if (sl != std::wstring::npos) name = name.substr(sl + 1);
    textIn(dc, { card.left + 12, card.top + 10, card.right - 12, card.top + 30 }, name, Theme::text, DT_SINGLELINE | DT_END_ELLIPSIS, fBold);
    wchar_t b[96];
    int ri = std::clamp(wd(CID_RES)->sel, 0, 2);
    const wchar_t* rn[3] = { L"480p", L"720p", L"1080p" };
    wfmt(b, L"MP4  •  %s  •  %s", rn[ri], wd(CID_FPS)->items[wd(CID_FPS)->sel].c_str());
    textIn(dc, { card.left + 12, card.top + 32, card.right - 12, card.top + 50 }, b, Theme::textDim, DT_SINGLELINE | DT_END_ELLIPSIS, fUI);
    RECT bar{ card.left + 12, card.top + 66, card.right - 12, card.top + 78 };
    fillRc(dc, bar, RGB(14, 14, 16));
    int pct = g_exporting ? g_exportPct : (g_lastExportOk ? 100 : g_exportPct);
    RECT fill{ bar.left, bar.top, bar.left + (rw(bar) * pct / 100), bar.bottom };
    fillRc(dc, fill, g_lastExportOk && !g_exporting ? RGB(70, 180, 100) : Theme::blue);
    std::wstring st = g_exporting ? L"Renderizando..." : (g_lastExportOk ? L"Completado" : (g_exportErr.empty() ? L"" : toWide(g_exportErr)));
    wfmt(b, L"%d%%", pct);
    textIn(dc, { card.left + 12, card.top + 88, card.right - 60, card.top + 110 }, st, Theme::text, DT_SINGLELINE | DT_END_ELLIPSIS, fUI);
    textIn(dc, { card.right - 60, card.top + 88, card.right - 12, card.top + 110 }, b, Theme::text, DT_RIGHT | DT_SINGLELINE, fUI);
}

// ---- Paint principal con buffer doble (CDC + CBitmap en memoria)
static CDC     g_memDC;
static CBitmap g_memBmp;
static CBitmap* g_memOld = nullptr;
static int g_memW = 0, g_memH = 0;

static void ensureBack(CDC* screen, int W, int H) {
    if (!g_memDC.GetSafeHdc()) g_memDC.CreateCompatibleDC(screen);
    if (!g_memBmp.GetSafeHandle() || g_memW != W || g_memH != H) {
        if (g_memOld) { g_memDC.SelectObject(g_memOld); g_memOld = nullptr; }
        g_memBmp.DeleteObject();
        g_memBmp.CreateCompatibleBitmap(screen, std::max(1, W), std::max(1, H));
        g_memOld = g_memDC.SelectObject(&g_memBmp);
        g_memW = W;
        g_memH = H;
    }
}

static void releaseBack() {
    if (g_memOld) { g_memDC.SelectObject(g_memOld); g_memOld = nullptr; }
    g_memBmp.DeleteObject();
    g_memDC.DeleteDC();
}

static void paint(CWnd* wnd, CDC* screen, const RECT& pr) {
    CRect rc;
    wnd->GetClientRect(&rc);
    ensureBack(screen, rc.right, rc.bottom);
    CDC* dc = &g_memDC;
    g_paintRc = pr;
    int saved = dc->SaveDC();
    dc->IntersectClipRect(pr.left, pr.top, pr.right, pr.bottom);
    dc->SetBkMode(TRANSPARENT);
    dc->SelectObject(fUI);
    fillRc(dc, rc, Theme::bg);

    drawTitleBar(dc);
    drawPool(dc);
    drawViewer(dc);
    drawInspector(dc);
    drawMeta(dc);
    drawToolsBar(dc);
    drawScopePanel(dc);
    drawColorPanel(dc);
    drawSettings(dc);
    drawQueue(dc);
    drawTimeline(dc);
    drawPageBar(dc, rc.right);

    dc->RestoreDC(saved);
    screen->BitBlt(pr.left, pr.top, rw(pr), rh(pr), dc, pr.left, pr.top, SRCCOPY);
}

// ---------------- CONTROLADOR: linea de tiempo ----------------
enum Drag { D_NONE, D_SCRUB, D_TRIML, D_TRIMR, D_MOVE, D_TXMOVE, D_TXL, D_TXR, D_AMOVE, D_AL, D_AR };
static Drag   g_drag = D_NONE;
static int    g_dragStartX = 0;
static bool   g_didUndo = false;
static double g_oIn = 0, g_oOut = 0, g_oStart = 0, g_oDur = 0;

static void onTimelineDown(CWnd* wnd, int mx, int my) {
    Hit h = hitTest(mx, my);
    g_dragStartX = mx;
    g_didUndo = false;

    switch (h.type) {
    case H_RULER:
        g_drag = D_SCRUB;
        wnd->SetCapture();
        setPlayhead(wnd, xToTime(mx));
        return;
    case H_CLIP: case H_CLIP_L: case H_CLIP_R: {
        g_selClip = h.idx;
        g_selText = g_selAudio = -1;
        const Clip& c = g_project.clips[h.idx];
        g_oIn = c.srcIn;
        g_oOut = c.srcOut;
        g_drag = (h.type == H_CLIP_L) ? D_TRIML : (h.type == H_CLIP_R ? D_TRIMR : D_MOVE);
        break;
    }
    case H_TEXT: case H_TEXT_L: case H_TEXT_R: {
        g_selText = h.idx;
        g_selClip = g_selAudio = -1;
        const TextItem& t = g_project.texts[h.idx];
        g_oStart = t.start;
        g_oDur = t.dur;
        g_drag = (h.type == H_TEXT_L) ? D_TXL : (h.type == H_TEXT_R ? D_TXR : D_TXMOVE);
        break;
    }
    case H_AUDIO: case H_AUDIO_L: case H_AUDIO_R: {
        g_selAudio = h.idx;
        g_selClip = g_selText = -1;
        const AudioClip& a = g_project.audio[h.idx];
        g_oIn = a.srcIn;
        g_oOut = a.srcOut;
        g_oStart = a.start;
        g_drag = (h.type == H_AUDIO_L) ? D_AL : (h.type == H_AUDIO_R ? D_AR : D_AMOVE);
        break;
    }
    default: // zona vacia: deseleccionar y mover el cabezal
        if (g_page != PG_COLOR) { g_selClip = g_selText = g_selAudio = -1; }
        g_drag = D_SCRUB;
        syncInspector();
        updateEnable();
        layoutAll(wnd);
        setPlayhead(wnd, xToTime(mx));
        wnd->SetCapture();
        invalidateTimeline(wnd);
        return;
    }
    wnd->SetCapture();
    syncInspector();
    updateEnable();
    layoutAll(wnd);
    wnd->InvalidateRect(nullptr, FALSE);
}

static void onTimelineMove(CWnd* wnd, int mx, int my) {
    if (g_drag == D_NONE) return;
    if (g_drag == D_SCRUB) { setPlayhead(wnd, xToTime(mx)); return; }
    if (std::abs(mx - g_dragStartX) < 3 && !g_didUndo) return;
    if (!g_didUndo) { pushUndo(); g_didUndo = true; }

    double dt = (mx - g_dragStartX) / g_pps;

    if (g_drag == D_AMOVE || g_drag == D_AL || g_drag == D_AR) {
        AudioClip* a = selAudio();
        if (!a) return;
        if (g_drag == D_AMOVE) {
            a->start = std::max(0.0, g_oStart + dt);
            a->track = (my >= track4Top() - 4) ? 1 : 0;   // arrastrar hacia A2 / A1
        } else if (g_drag == D_AL) {   // recorte por la izquierda: el contenido no se mueve
            double ns = std::clamp(g_oIn + dt, 0.0, g_oOut - 0.1);
            a->srcIn = ns;
            a->start = g_oStart + (ns - g_oIn);
        } else {
            double maxOut = std::max(g_oOut, g_project.media[a->media].audioDur);
            a->srcOut = std::clamp(g_oOut + dt, g_oIn + 0.1, maxOut);
        }
    } else if (g_drag == D_TRIML || g_drag == D_TRIMR || g_drag == D_MOVE) {
        Clip* c = selClip();
        if (!c) return;
        if (g_drag == D_TRIML) {
            c->srcIn = std::clamp(g_oIn + dt * c->speed, 0.0, g_oOut - 0.2 * c->speed);
        } else if (g_drag == D_TRIMR) {
            double maxOut = g_project.media[c->media].duration;
            c->srcOut = std::clamp(g_oOut + dt * c->speed, g_oIn + 0.2 * c->speed, maxOut);
        } else { // reordenar arrastrando
            double tm = xToTime(mx);
            double s = 0;
            int newIdx = 0;
            for (int k = 0; k < (int)g_project.clips.size(); ++k) {
                if (k == g_selClip) continue;
                double d = g_project.clips[k].duration();
                if (tm > s + d / 2) newIdx++;
                s += d;
            }
            if (newIdx != g_selClip) {
                Clip moved = g_project.clips[g_selClip];
                g_project.clips.erase(g_project.clips.begin() + g_selClip);
                g_project.clips.insert(g_project.clips.begin() + newIdx, moved);
                g_selClip = newIdx;
            }
        }
    } else {
        TextItem* t = selText();
        if (!t) return;
        if (g_drag == D_TXMOVE) {
            t->start = std::max(0.0, g_oStart + dt);
        } else if (g_drag == D_TXR) {
            t->dur = std::max(0.3, g_oDur + dt);
        } else {
            double ns = std::clamp(g_oStart + dt, 0.0, g_oStart + g_oDur - 0.3);
            t->dur = g_oDur - (ns - g_oStart);
            t->start = ns;
        }
    }
    g_playhead = std::clamp(g_playhead, 0.0, g_project.duration());
    renderPreview(wnd);
    invalidateTimeline(wnd);
}

// ---------------- CONTROLADOR: widgets ----------------
static void applyWidget(CWnd* wnd, int id) {
    Widget* w = g_w[id];
    if (id == CID_TLZOOM) {
        g_pps = w->val;
        clampScroll();
        invalidateTimeline(wnd);
        return;
    }
    if (Clip* c = selClip()) {
        switch (id) {
        case CID_ZOOM:     c->zoom = (int)w->val; break;
        case CID_POSX:     c->posX = (int)w->val; break;
        case CID_POSY:     c->posY = (int)w->val; break;
        case CID_BRIGHT:   c->brightness = (int)w->val; break;
        case CID_CONTRAST: c->contrast = (int)w->val; break;
        case CID_SAT:      c->saturation = (int)w->val; break;
        case CID_TEMP:     c->temp = (int)w->val; break;
        case CID_TINT:     c->tint = (int)w->val; break;
        case CID_WH_LIFT:  c->lift = { w->wx, w->wy, w->val }; break;
        case CID_WH_GAMMA: c->gamma = { w->wx, w->wy, w->val }; break;
        case CID_WH_GAIN:  c->gain = { w->wx, w->wy, w->val }; break;
        default: break;
        }
    } else if (AudioClip* a = selAudio()) {
        switch (id) {
        case CID_A_VOL:  a->volume = (int)w->val; break;
        case CID_A_FIN:  a->fadeIn = w->val; break;
        case CID_A_FOUT: a->fadeOut = w->val; break;
        default: break;
        }
    } else if (TextItem* t = selText()) {
        switch (id) {
        case CID_TXT_SIZE: t->size = (int)w->val; break;
        case CID_TXT_X:    t->x = w->val / 100.0; break;
        case CID_TXT_Y:    t->y = w->val / 100.0; break;
        default: break;
        }
    }
    renderPreview(wnd);
    invalidateTimeline(wnd);
}

static void resetGrade(Clip& c) {
    c.brightness = c.contrast = c.saturation = c.temp = c.tint = 0;
    c.lift = c.gamma = c.gain = Wheel{};
}

static void onWidget(CWnd* wnd, int id, int ev) {
    Widget* w = findWidget(id);
    if (!w) return;

    if (w->type == WT_SLIDER || w->type == WT_WHEEL) {
        if (ev == EV_BEGIN) { if (id != CID_TLZOOM) pushUndo(); }
        else if (ev == EV_CHANGE) applyWidget(wnd, id);
        else if (ev == EV_END) { updateEnable(); wnd->SetFocus(); }
        return;
    }
    if (ev != EV_CLICK) return;

    switch (id) {
    case CID_IMPORT: doImport(wnd); break;
    case CID_ADDCLIP: addClip(wnd, g_selMedia); break;
    case CID_ADDAUDIO: addAudio(wnd, g_selMedia); break;
    case CID_A_MUTE:
        if (selAudio()) { pushUndo(); selAudio()->mute = w->checked; afterEdit(wnd); }
        break;
    case CID_PLAY: togglePlay(wnd); break;
    case CID_HOME: setPlayhead(wnd, 0.0); followPlayhead(); break;
    case CID_END: setPlayhead(wnd, g_project.duration()); followPlayhead(); break;
    case CID_SPLIT: doSplit(wnd); break;
    case CID_DELETE: doDelete(wnd); break;
    case CID_ADDTEXT: doAddText(wnd); return; // el foco queda en el cuadro de texto
    case CID_UNDO: doUndo(wnd); break;
    case CID_REDO: doRedo(wnd); break;
    case CID_RESETFX:
        if (selClip()) {
            pushUndo();
            Clip* c = selClip();
            resetGrade(*c);
            c->filter = Filter::None; c->speed = 1.0; c->trans = Trans::None;
            c->zoom = 100; c->posX = c->posY = 0;
            afterEdit(wnd);
        }
        break;
    case CID_RESETGRADE:
        if (selClip()) { pushUndo(); resetGrade(*selClip()); afterEdit(wnd); }
        break;
    case CID_SPEED: case CID_FILTER: case CID_TRANS: case CID_TRANSDUR:
        if (selClip()) {
            pushUndo();
            Clip* c = selClip();
            if (id == CID_SPEED)    c->speed = kSpeeds[std::clamp(w->sel, 0, 7)];
            if (id == CID_FILTER)   c->filter = (Filter)std::clamp(w->sel, 0, 3);
            if (id == CID_TRANS)    c->trans = (Trans)std::clamp(w->sel, 0, 1);
            if (id == CID_TRANSDUR) c->transDur = kTransDurs[std::clamp(w->sel, 0, 2)];
            afterEdit(wnd);
        }
        break;
    case CID_TXT_COLOR:
        if (selText()) { pushUndo(); selText()->rgb = kColors[std::clamp(w->sel, 0, 5)].rgb; afterEdit(wnd); }
        break;
    case CID_TXT_BOX:
        if (selText()) { pushUndo(); selText()->box = w->checked; renderPreview(wnd); }
        break;
    case CID_SCOPE:
        g_scopeMode = w->sel;
        renderPreview(wnd);
        break;
    case CID_FPS: {
        static const int kFps[4] = { 24, 25, 30, 60 };
        g_project.fps = kFps[std::clamp(w->sel, 0, 3)];
        wnd->InvalidateRect(nullptr, FALSE);
        break;
    }
    case CID_RES: case CID_QUALITY: wnd->InvalidateRect(&g_rcQueue, FALSE); break;
    case CID_BROWSE: {
        std::wstring p;
        if (pickFile(wnd, true, L"Video MP4\0*.mp4\0", L"mp4", p)) { g_outPath = p; wnd->InvalidateRect(&g_rcSettings, FALSE); }
        break;
    }
    case CID_RENDER: doExport(wnd); break;
    }
    wnd->SetFocus();
}

// ---------------- Ventana principal ----------------
static Widget* mkW(CWnd* p, WType t, int id, const wchar_t* text) {
    Widget* w = createWidget(p, t, id, text);
    g_w[id] = w;
    return w;
}
static Widget* mkSlider(CWnd* p, int id, const wchar_t* text, double mn, double mx, double def, const wchar_t* fmt) {
    Widget* w = mkW(p, WT_SLIDER, id, text);
    w->minv = mn; w->maxv = mx; w->def = def; w->val = def; w->fmt = fmt;
    return w;
}
static Widget* mkDrop(CWnd* p, int id, const wchar_t* text, std::vector<std::wstring> items, int sel = 0) {
    Widget* w = mkW(p, WT_DROP, id, text);
    w->items = std::move(items);
    w->sel = sel;
    return w;
}

static void createControls(CWnd* wnd) {
    mkW(wnd, WT_BUTTON, CID_IMPORT, L"Importar medios");
    mkW(wnd, WT_BUTTON, CID_ADDCLIP, L"+ Video")->primary = true;
    mkW(wnd, WT_BUTTON, CID_ADDAUDIO, L"+ Audio");
    mkW(wnd, WT_BUTTON, CID_HOME, L"")->icon = IC_PREV;
    mkW(wnd, WT_BUTTON, CID_PLAY, L"")->icon = IC_PLAY;
    mkW(wnd, WT_BUTTON, CID_END, L"")->icon = IC_NEXT;
    mkW(wnd, WT_BUTTON, CID_SPLIT, L"Dividir");
    mkW(wnd, WT_BUTTON, CID_DELETE, L"Eliminar");
    mkW(wnd, WT_BUTTON, CID_ADDTEXT, L"+ Título");
    mkW(wnd, WT_BUTTON, CID_UNDO, L"Deshacer");
    mkW(wnd, WT_BUTTON, CID_REDO, L"Rehacer");
    mkSlider(wnd, CID_TLZOOM, L"", 20, 300, 80, L"%.0f")->step = 1;

    mkDrop(wnd, CID_SPEED, L"Velocidad", { L"0.25x (muy lento)", L"0.5x (lento)", L"0.75x", L"1x (normal)", L"1.5x", L"2x (rápido)", L"3x", L"4x" }, 3);
    mkSlider(wnd, CID_ZOOM, L"Zoom", 10, 400, 100, L"%.0f%%");
    mkSlider(wnd, CID_POSX, L"Posición X", -100, 100, 0, L"%+.0f");
    mkSlider(wnd, CID_POSY, L"Posición Y", -100, 100, 0, L"%+.0f");
    mkSlider(wnd, CID_BRIGHT, L"Brillo", -100, 100, 0, L"%+.0f");
    mkSlider(wnd, CID_CONTRAST, L"Contraste", -100, 100, 0, L"%+.0f");
    mkSlider(wnd, CID_SAT, L"Saturación", -100, 100, 0, L"%+.0f");
    mkSlider(wnd, CID_TEMP, L"Temperatura", -100, 100, 0, L"%+.0f");
    mkSlider(wnd, CID_TINT, L"Tinte", -100, 100, 0, L"%+.0f");
    mkDrop(wnd, CID_FILTER, L"Filtro", { L"Ninguno", L"Escala de grises", L"Bordes (Sobel)", L"Invertir colores" });
    mkDrop(wnd, CID_TRANS, L"Transición", { L"Ninguna", L"Fundido a negro" });
    mkDrop(wnd, CID_TRANSDUR, L"Duración", { L"0.5 s", L"1 s", L"2 s" });
    mkW(wnd, WT_BUTTON, CID_RESETFX, L"Restablecer clip");
    mkW(wnd, WT_BUTTON, CID_RESETGRADE, L"Restablecer color");

    mkSlider(wnd, CID_TXT_SIZE, L"Tamaño", 2, 20, 8, L"%.0f%%");
    mkSlider(wnd, CID_TXT_X, L"Posición X", 0, 100, 50, L"%.0f%%");
    mkSlider(wnd, CID_TXT_Y, L"Posición Y", 0, 100, 85, L"%.0f%%");
    mkDrop(wnd, CID_TXT_COLOR, L"Color", { kColors[0].name, kColors[1].name, kColors[2].name, kColors[3].name, kColors[4].name, kColors[5].name });
    mkW(wnd, WT_TOGGLE, CID_TXT_BOX, L"Fondo oscuro detrás del texto");

    mkSlider(wnd, CID_A_VOL, L"Volumen", 0, 200, 100, L"%.0f%%");
    mkSlider(wnd, CID_A_FIN, L"Entrada", 0, 10, 0, L"%.1f s")->step = 0.1;
    mkSlider(wnd, CID_A_FOUT, L"Salida", 0, 10, 0, L"%.1f s")->step = 0.1;
    mkW(wnd, WT_TOGGLE, CID_A_MUTE, L"Silenciar este clip de audio");

    const wchar_t* wn[3] = { L"Sombras  (Lift)", L"Medios  (Gamma)", L"Luces  (Gain)" };
    for (int i = 0; i < 3; ++i) {
        Widget* w = mkW(wnd, WT_WHEEL, CID_WH_LIFT + i, wn[i]);
        w->minv = -1; w->maxv = 1; w->def = 0; w->step = 0.01;
    }
    mkDrop(wnd, CID_SCOPE, L"", { L"Forma de onda", L"Parade RGB", L"Histograma", L"Vectorscopio" });

    mkDrop(wnd, CID_RES, L"Resolución", { L"854 × 480  (480p)", L"1280 × 720  (HD)", L"1920 × 1080  (Full HD)" }, 1);
    mkDrop(wnd, CID_FPS, L"Fotogramas", { L"24 fps", L"25 fps", L"30 fps", L"60 fps" }, 2);
    mkDrop(wnd, CID_QUALITY, L"Calidad", { L"Baja", L"Media", L"Alta" }, 1);
    mkW(wnd, WT_BUTTON, CID_BROWSE, L"Examinar...");
    mkW(wnd, WT_BUTTON, CID_RENDER, L"Renderizar")->primary = true;

    for (int id : { CID_ZOOM, CID_POSX, CID_POSY, CID_BRIGHT, CID_CONTRAST, CID_SAT, CID_TEMP, CID_TINT }) g_w[id]->labelW = 100;
    for (int id : { CID_SPEED, CID_FILTER, CID_TRANS, CID_TRANSDUR, CID_TXT_COLOR, CID_RES, CID_FPS, CID_QUALITY }) g_w[id]->labelW = 100;
    for (int id : { CID_TXT_SIZE, CID_TXT_X, CID_TXT_Y, CID_A_VOL, CID_A_FIN, CID_A_FOUT }) g_w[id]->labelW = 100;

    // Cuadro de texto del inspector: control MFC (CEdit) hijo de la ventana principal
    g_editCtl.Create(WS_CHILD | WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
                     CRect(0, 0, 10, 10), wnd, CID_TXT_EDIT);
    g_editCtl.SetFont(fUI);
}

// =====================================================================
//  Ventana principal MFC: CMainFrame (CFrameWnd) con mapa de mensajes.
//  Toda la logica del editor (modelo, render, linea de tiempo) vive en las
//  funciones de arriba; aqui MFC solo enruta los eventos de la ventana.
// =====================================================================
class CMainFrame : public CFrameWnd {
public:
    CMainFrame() = default;

protected:
    BOOL PreCreateWindow(CREATESTRUCT& cs) override {
        if (!CFrameWnd::PreCreateWindow(cs)) return FALSE;
        cs.style = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;
        cs.dwExStyle &= ~WS_EX_CLIENTEDGE;
        cs.lpszClass = AfxRegisterWndClass(CS_DBLCLKS, ::LoadCursorW(nullptr, IDC_ARROW), nullptr, nullptr);
        return TRUE;
    }

    afx_msg int  OnCreate(LPCREATESTRUCT cs);
    afx_msg void OnDestroy();
    afx_msg void OnClose();
    afx_msg void OnPaint();
    afx_msg BOOL OnEraseBkgnd(CDC*) { return TRUE; }
    afx_msg void OnSize(UINT type, int cx, int cy);
    afx_msg void OnGetMinMaxInfo(MINMAXINFO* mm);
    afx_msg void OnLButtonDown(UINT, CPoint pt)   { leftDown(pt, false); }
    afx_msg void OnLButtonDblClk(UINT, CPoint pt) { leftDown(pt, true); }
    afx_msg void OnMouseMove(UINT, CPoint pt);
    afx_msg void OnLButtonUp(UINT, CPoint);
    afx_msg BOOL OnSetCursor(CWnd* w, UINT hit, UINT msg);
    afx_msg BOOL OnMouseWheel(UINT flags, short delta, CPoint screenPt);
    afx_msg void OnKeyDown(UINT key, UINT rep, UINT flags);
    afx_msg void OnTimer(UINT_PTR id);
    afx_msg void OnDropFiles(HDROP drop);
    afx_msg HBRUSH OnCtlColor(CDC* dc, CWnd* wnd, UINT type);
    afx_msg LRESULT OnWidgetMsg(WPARAM wp, LPARAM lp);
    afx_msg LRESULT OnExportProgress(WPARAM wp, LPARAM lp);
    afx_msg LRESULT OnExportDone(WPARAM wp, LPARAM lp);
    afx_msg void OnTextSetFocus();
    afx_msg void OnTextChange();

    DECLARE_MESSAGE_MAP()

private:
    void leftDown(CPoint pt, bool dbl);
};

BEGIN_MESSAGE_MAP(CMainFrame, CFrameWnd)
    ON_WM_CREATE()
    ON_WM_DESTROY()
    ON_WM_CLOSE()
    ON_WM_PAINT()
    ON_WM_ERASEBKGND()
    ON_WM_SIZE()
    ON_WM_GETMINMAXINFO()
    ON_WM_LBUTTONDOWN()
    ON_WM_LBUTTONDBLCLK()
    ON_WM_MOUSEMOVE()
    ON_WM_LBUTTONUP()
    ON_WM_SETCURSOR()
    ON_WM_MOUSEWHEEL()
    ON_WM_KEYDOWN()
    ON_WM_TIMER()
    ON_WM_DROPFILES()
    ON_WM_CTLCOLOR()
    ON_MESSAGE(WM_WIDGET, OnWidgetMsg)
    ON_MESSAGE(WM_EXPORT_PROGRESS, OnExportProgress)
    ON_MESSAGE(WM_EXPORT_DONE, OnExportDone)
    ON_EN_SETFOCUS(CID_TXT_EDIT, OnTextSetFocus)
    ON_EN_CHANGE(CID_TXT_EDIT, OnTextChange)
END_MESSAGE_MAP()

int CMainFrame::OnCreate(LPCREATESTRUCT cs) {
    if (CFrameWnd::OnCreate(cs) == -1) return -1;
    CWnd* wnd = this;
    createFonts();
    g_editBrush.CreateSolidBrush(Theme::field);
    BOOL dark = TRUE;
    DwmSetWindowAttribute(m_hWnd, 20, &dark, sizeof(dark)); // barra de titulo oscura (Windows 10 2004+ / 11)
    DwmSetWindowAttribute(m_hWnd, 19, &dark, sizeof(dark));
    g_canvas.create(640, 360);
    g_srcCanvas.create(640, 360);
    g_scopeCanvas.create(512, 256);
    createControls(wnd);
    DragAcceptFiles(TRUE);
    layoutAll(wnd);
    g_renderer.render(g_project, 0.0, g_canvas);
    updateEnable();
    syncInspector();
    SetFocus();
    return 0;
}

LRESULT CMainFrame::OnWidgetMsg(WPARAM wp, LPARAM lp) {
    onWidget(this, (int)wp, (int)lp);
    return 0;
}

HBRUSH CMainFrame::OnCtlColor(CDC* dc, CWnd* wnd, UINT type) {
    if (wnd && wnd->GetSafeHwnd() == g_editCtl.GetSafeHwnd()) {
        dc->SetTextColor(Theme::text);
        dc->SetBkColor(Theme::field);
        return (HBRUSH)g_editBrush.GetSafeHandle();
    }
    return CFrameWnd::OnCtlColor(dc, wnd, type);
}

void CMainFrame::OnTextSetFocus() { g_editUndoPending = true; }

void CMainFrame::OnTextChange() {
    if (g_syncing) return;
    if (TextItem* t = selText()) {
        if (g_editUndoPending) { pushUndo(); g_editUndoPending = false; t = selText(); }
        CString s;
        g_editCtl.GetWindowText(s);
        t->text = s.GetString();
        updateEnable();
        renderPreview(this);
        invalidateTimeline(this);
    }
}

void CMainFrame::leftDown(CPoint p, bool dbl) {
    CWnd* wnd = this;
    SetFocus();
    int mx = p.x, my = p.y;
    POINT pt{ mx, my };
    if (PtInRect(&g_rcPageBar, pt)) {
        for (int i = 0; i < 4; ++i) { RECT t = tabRect(i); if (PtInRect(&t, pt)) { setPage(wnd, i); break; } }
    } else if (PtInRect(&g_rcTimeline, pt)) {
        onTimelineDown(wnd, mx, my);
    } else if (rw(g_rcPool) > 0 && PtInRect(&g_rcPool, pt)) {
        int i = poolHit(mx, my);
        if (i >= 0) {
            g_selMedia = i;
            if (g_page == PG_MEDIA) refreshSourcePreview();
            updateEnable();
            InvalidateRect(nullptr, FALSE);
            if (dbl) {
                if (g_project.media[i].hasVideo) addClip(wnd, i);
                else addAudio(wnd, i);
            }
        }
    } else if (rw(g_rcViewImg) > 0 && g_page != PG_MEDIA && PtInRect(&g_rcViewImg, pt)) {
        togglePlay(wnd);
    }
}

void CMainFrame::OnMouseMove(UINT, CPoint pt) {
    if (g_drag != D_NONE) onTimelineMove(this, pt.x, pt.y);
}

void CMainFrame::OnLButtonUp(UINT, CPoint) {
    if (g_drag != D_NONE) {
        g_drag = D_NONE;
        ReleaseCapture();
        afterEdit(this);
    }
}

BOOL CMainFrame::OnSetCursor(CWnd* w, UINT hit, UINT msg) {
    if (hit == HTCLIENT) {
        CPoint pt;
        GetCursorPos(&pt);
        ScreenToClient(&pt);
        Hit h = hitTest(pt.x, pt.y);
        bool edge = h.type == H_CLIP_L || h.type == H_CLIP_R || h.type == H_TEXT_L || h.type == H_TEXT_R ||
                    h.type == H_AUDIO_L || h.type == H_AUDIO_R ||
                    g_drag == D_TRIML || g_drag == D_TRIMR || g_drag == D_TXL || g_drag == D_TXR || g_drag == D_AL || g_drag == D_AR;
        if (edge) { ::SetCursor(::LoadCursorW(nullptr, IDC_SIZEWE)); return TRUE; }
        if (h.type == H_CLIP || h.type == H_TEXT || h.type == H_AUDIO) { ::SetCursor(::LoadCursorW(nullptr, IDC_SIZEALL)); return TRUE; }
    }
    return CFrameWnd::OnSetCursor(w, hit, msg);
}

BOOL CMainFrame::OnMouseWheel(UINT flags, short delta, CPoint screenPt) {
    CWnd* wnd = this;
    CPoint p = screenPt;
    ScreenToClient(&p);
    POINT pt{ p.x, p.y };
    if (PtInRect(&g_rcTimeline, pt)) {
        if (flags & MK_CONTROL) {
            double tm = xToTime(pt.x);
            g_pps = std::clamp(g_pps * (delta > 0 ? 1.2 : 1.0 / 1.2), 20.0, 300.0);
            g_scrollX = g_rcTimeline.left + TL_HDR + TL_PAD + (int)(tm * g_pps) - pt.x;
            g_w[CID_TLZOOM]->val = g_pps;
            invalidateWidget(g_w[CID_TLZOOM]);
        } else {
            g_scrollX -= delta / 120 * 90;
        }
        clampScroll();
        invalidateTimeline(wnd);
    } else if (rw(g_rcPool) > 0 && PtInRect(&g_rcPool, pt)) {
        g_poolScroll -= delta / 120 * 60;
        clampPoolScroll();
        InvalidateRect(&g_rcPool, FALSE);
    }
    return TRUE;
}

void CMainFrame::OnKeyDown(UINT key, UINT rep, UINT flags) {
    CWnd* wnd = this;
    WPARAM wp = key;
    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    if (shift && !ctrl) { // paginas como en Resolve: Shift+2 Medios, +4 Edicion, +6 Color, +8 Entrega
        if (wp == '2') { setPage(wnd, PG_MEDIA); return; }
        if (wp == '4') { setPage(wnd, PG_EDIT); return; }
        if (wp == '6') { setPage(wnd, PG_COLOR); return; }
        if (wp == '8') { setPage(wnd, PG_DELIVER); return; }
    }
    if (ctrl && wp == 'Z') { doUndo(wnd); return; }
    if (ctrl && wp == 'Y') { doRedo(wnd); return; }
    if (ctrl && wp == 'I') { doImport(wnd); return; }
    if (ctrl && wp == 'E') { setPage(wnd, PG_DELIVER); return; }
    if (ctrl && wp == 'B') { doSplit(wnd); return; }
    if (wp == VK_SPACE || wp == 'L') { togglePlay(wnd); return; }
    if (wp == 'K') { if (g_playing) stopPlay(wnd); return; }
    if (wp == 'S' && !ctrl) { doSplit(wnd); return; }
    if (wp == VK_DELETE) { doDelete(wnd); return; }
    if (wp == VK_HOME) { setPlayhead(wnd, 0.0); followPlayhead(); return; }
    if (wp == VK_END) { setPlayhead(wnd, g_project.duration()); followPlayhead(); return; }
    if (wp == VK_UP) { jumpEdit(wnd, -1); return; }
    if (wp == VK_DOWN) { jumpEdit(wnd, +1); return; }
    if (wp == VK_LEFT || wp == VK_RIGHT) {
        double step = shift ? 1.0 : 1.0 / std::max(1.0, g_project.fps);
        setPlayhead(wnd, g_playhead + (wp == VK_LEFT ? -step : step));
        followPlayhead();
        return;
    }
    CFrameWnd::OnKeyDown(key, rep, flags);
}

void CMainFrame::OnTimer(UINT_PTR id) {
    CWnd* wnd = this;
    if (id == (UINT_PTR)CID_TIMER && g_playing) {
        double t;
        if (g_engine.running()) {
            g_engine.pump();
            t = std::max(g_playhead, g_engine.position());   // el reloj de audio manda
        } else {
            t = g_playStartT + (GetTickCount64() - g_playStartTick) / 1000.0;
        }
        double dur = g_project.duration();
        if (t >= dur) { t = dur; stopPlay(wnd); }
        g_playhead = t;
        followPlayhead();
        renderPreview(wnd);
        invalidateTimeline(wnd);
    }
}

LRESULT CMainFrame::OnExportProgress(WPARAM wp, LPARAM) {
    g_exportPct = (int)wp;
    InvalidateRect(&g_rcQueue, FALSE);
    invalidateBar(this);
    return 0;
}

LRESULT CMainFrame::OnExportDone(WPARAM wp, LPARAM) {
    if (g_worker.joinable()) g_worker.join();
    g_exporting = false;
    g_lastExportOk = (wp != 0);
    wd(CID_RENDER)->text = L"Renderizar";
    invalidateWidget(wd(CID_RENDER));
    if (wp) {
        g_exportPct = 100;
        g_msg = L"Video exportado: " + g_outPath;
        msgBox(this, (L"Video exportado correctamente:\n" + g_outPath).c_str(), L"VideoLab", MB_ICONINFORMATION);
    } else {
        g_exportPct = 0;
        g_msg = toWide(g_exportErr);
        if (g_exportErr != "Exportacion cancelada.")
            msgBox(this, toWide(g_exportErr).c_str(), L"No se pudo exportar", MB_ICONERROR);
    }
    InvalidateRect(nullptr, FALSE);
    return 0;
}

void CMainFrame::OnDropFiles(HDROP drop) {
    UINT n = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
    for (UINT i = 0; i < n; ++i) {
        wchar_t path[MAX_PATH];
        if (DragQueryFileW(drop, i, path, MAX_PATH)) importFile(this, path);
    }
    DragFinish(drop);
}

void CMainFrame::OnGetMinMaxInfo(MINMAXINFO* mm) {
    mm->ptMinTrackSize.x = 1280;
    mm->ptMinTrackSize.y = 800;
}

void CMainFrame::OnPaint() {
    CPaintDC dc(this);   // BeginPaint / EndPaint
    paint(this, &dc, dc.m_ps.rcPaint);
}

void CMainFrame::OnSize(UINT type, int, int) {
    if (type != SIZE_MINIMIZED && g_editCtl.GetSafeHwnd()) {
        layoutAll(this);
        InvalidateRect(nullptr, FALSE);
    }
}

void CMainFrame::OnClose() {
    if (g_exporting) {
        if (msgBox(this, L"Hay un render en curso. Se cancelará si sales. ¿Quieres salir?", L"VideoLab",
                   MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    }
    CFrameWnd::OnClose();
}

void CMainFrame::OnDestroy() {
    g_cancel = true;
    if (g_worker.joinable()) g_worker.join();
    KillTimer(CID_TIMER);
    g_engine.stop();
    g_thumbs.clear();
    g_canvas.destroy();
    g_srcCanvas.destroy();
    g_scopeCanvas.destroy();
    releaseBack();
    g_editBrush.DeleteObject();
    destroyFonts();
    CFrameWnd::OnDestroy();
}

// =====================================================================
//  Aplicacion MFC
// =====================================================================
class CVideoLabApp : public CWinApp {
public:
    BOOL InitInstance() override {
        CWinApp::InitInstance();
        auto* frame = new CMainFrame();
        m_pMainWnd = frame;
        if (!frame->Create(nullptr, L"VideoLab", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CFrameWnd::rectDefault))
            return FALSE;
        frame->SetWindowPos(nullptr, 0, 0, 1440, 900, SWP_NOMOVE | SWP_NOZORDER);
        frame->CenterWindow();
        frame->ShowWindow(m_nCmdShow);
        frame->UpdateWindow();
        return TRUE;
    }
};

CVideoLabApp theApp;   // el objeto de aplicacion global arranca el bucle de mensajes de MFC
