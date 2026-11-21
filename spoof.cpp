#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define _WIN32_WINNT 0x0A00
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <vector>
#include <string>
#include <sstream>
#include <algorithm>
#include <cstdio>
#include <cstring>

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#define DWMWCP_DONOTROUND 2
#endif
#ifndef DWMWA_CAPTION_COLOR
#define DWMWA_CAPTION_COLOR 35
#endif
#ifndef DWMWA_TEXT_COLOR
#define DWMWA_TEXT_COLOR 36
#endif

#define WM_LOG      (WM_APP + 1)
#define WM_DONE     (WM_APP + 2)
#define WM_ADAPTERS (WM_APP + 3)

static inline COLORREF C(unsigned char r, unsigned char g, unsigned char b) { return RGB(r,g,b); }
namespace Pal {
    static const COLORREF bg      = C(14, 17, 23);
    static const COLORREF panel   = C(23, 28, 38);
    static const COLORREF panel2  = C(29, 35, 47);
    static const COLORREF hover   = C(38, 47, 62);
    static const COLORREF line    = C(46, 56, 73);
    static const COLORREF text    = C(234, 239, 247);
    static const COLORREF dim     = C(139, 152, 172);
    static const COLORREF accent  = C(0, 216, 160);
    static const COLORREF accentH = C(0, 242, 184);
    static const COLORREF cyan    = C(90, 200, 250);
    static const COLORREF amber   = C(255, 180, 84);
    static const COLORREF red     = C(255, 92, 108);
    static const COLORREF purple  = C(199, 146, 234);
}

struct Adapter {
    std::wstring name, desc, mac, status, guid, pnpid;
    bool disabled = false;
};
struct LogLine { std::wstring text; COLORREF color; };

static HWND  g_hwnd = nullptr;
static HINSTANCE g_hInst = nullptr;
static std::wstring g_psPath;

static std::vector<Adapter> g_adapters;
static std::vector<LogLine> g_log;
static int  g_sel = -1;
static int  g_listScroll = 0;
static int  g_logScroll = 0;
static bool g_busy = false;
static int  g_hover = -1;
static bool g_overRefresh = false;
static int  g_lastMode = 0;
static std::wstring g_status = L"READY";
static COLORREF g_statusColor = Pal::accent;

static std::wstring g_macInput;
static bool g_macEditing = false;
static bool g_caretOn = true;

static HFONT g_fTitle, g_fSub, g_fLabel, g_fBody, g_fSmall, g_fMono, g_fBtn;

static const int CW = 1040, CH = 652;
static const int PAD = 16;
static const int CONTENT_W = CW - 2*PAD;
static const int HEADER_H = 64;
static const int PANEL_Y = 80, PANEL_H = 452;
static const int LEFT_X = 16, LEFT_W = 374;
static const int RIGHT_X = 406, RIGHT_W = 618;
static const int BTN_Y = 544, BTN_H = 52, BTN_GAP = 10;
static const int STATUS_Y = 606, STATUS_H = 30;
static const int ROW_H = 52;
static const int LOG_LINE_H = 19;
static const int NB = 5;

static const wchar_t* BTN_LABELS[NB] = {
    L"SPOOF MAC", L"RESTORE", L"REVIVE", L"FULL STEALTH", L"CLEAR LOG"
};
static const COLORREF BTN_ACCENT[NB] = {
    Pal::accent, Pal::cyan, Pal::amber, Pal::purple, Pal::dim
};

static RECT R(int x, int y, int w, int h) { RECT r; r.left=x; r.top=y; r.right=x+w; r.bottom=y+h; return r; }
static bool In(const RECT& r, int x, int y) { return x>=r.left && x<r.right && y>=r.top && y<r.bottom; }

static RECT HeaderRect()    { return R(0, 0, CW, HEADER_H); }
static RECT LeftPanel()     { return R(LEFT_X, PANEL_Y, LEFT_W, PANEL_H); }
static RECT RightPanel()    { return R(RIGHT_X, PANEL_Y, RIGHT_W, PANEL_H); }
static RECT RefreshRect()   { return R(LEFT_X+LEFT_W-92, PANEL_Y+8, 78, 26); }
static RECT ListRect()      { return R(LEFT_X+10, PANEL_Y+40, LEFT_W-20, 268); }
static RECT LogRect()       { return R(RIGHT_X+10, PANEL_Y+40, RIGHT_W-20, PANEL_H-52); }
static RECT MacEditRect()   { return R(LEFT_X+104, PANEL_Y+406, LEFT_W-118, 28); }
static RECT StatusRect()    { return R(PAD, STATUS_Y, CONTENT_W, STATUS_H); }
static RECT ButtonRect(int i) {
    const int bw = (CONTENT_W - (NB-1)*BTN_GAP) / NB;
    const int x = PAD + i*(bw + BTN_GAP);
    const int w = bw + ((i == NB-1) ? (CONTENT_W - (NB*bw + (NB-1)*BTN_GAP)) : 0);
    return R(x, BTN_Y, w, BTN_H);
}
static int HitButton(int x, int y) {
    for (int i=0; i<NB; i++) if (In(ButtonRect(i), x, y)) return i;
    return -1;
}

static void Fill(HDC dc, int x, int y, int w, int h, COLORREF col) {
    HBRUSH b = CreateSolidBrush(col);
    RECT r = R(x,y,w,h);
    FillRect(dc, &r, b);
    DeleteObject(b);
}
static void Frame(HDC dc, int x, int y, int w, int h, COLORREF col) {
    HBRUSH b = CreateSolidBrush(col);
    RECT r = R(x,y,w,h);
    FrameRect(dc, &r, b);
    DeleteObject(b);
}
static void DrawTxt(HDC dc, const wchar_t* s, int x, int y, int w, int h,
                    HFONT f, COLORREF col, UINT flags = DT_LEFT|DT_VCENTER|DT_SINGLELINE) {
    HFONT old = (HFONT)SelectObject(dc, f);
    SetTextColor(dc, col);
    SetBkMode(dc, TRANSPARENT);
    RECT r = R(x,y,w,h);
    DrawTextW(dc, s, -1, &r, flags);
    SelectObject(dc, old);
}
static void Line(HDC dc, int x1, int y1, int x2, int y2, COLORREF col) {
    HPEN p = CreatePen(PS_SOLID, 1, col);
    HGDIOBJ o = SelectObject(dc, p);
    MoveToEx(dc, x1, y1, nullptr);
    LineTo(dc, x2, y2);
    SelectObject(dc, o);
    DeleteObject(p);
}
static HFONT Font(int px, int weight, const wchar_t* face, bool mono = false) {
    return CreateFontW(px, 0, 0, 0, weight, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        mono ? FIXED_PITCH|FF_MODERN : DEFAULT_PITCH|FF_SWISS, face);
}
static void InitFonts() {
    g_fTitle = Font(27, FW_BOLD, L"Segoe UI");
    g_fSub   = Font(14, FW_NORMAL, L"Segoe UI");
    g_fLabel = Font(14, FW_BOLD, L"Segoe UI");
    g_fBody  = Font(15, FW_BOLD, L"Segoe UI");
    g_fSmall = Font(13, FW_NORMAL, L"Segoe UI");
    g_fBtn   = Font(15, FW_BOLD, L"Segoe UI");
    g_fMono  = Font(14, FW_NORMAL, L"Consolas", true);
}

static COLORREF ColorFor(const std::wstring& s) {
    if (s.find(L"[SUCCESS]") != std::wstring::npos) return Pal::accent;
    if (s.find(L"[ERROR]")   != std::wstring::npos) return Pal::red;
    if (s.find(L"[!]")       != std::wstring::npos ||
        s.find(L"[WARNING]") != std::wstring::npos) return Pal::amber;
    if (s.find(L"[*]") != std::wstring::npos) return Pal::cyan;
    if (s.find(L"===") != std::wstring::npos) return Pal::accent;
    return Pal::dim;
}
static void AppendLog(const std::wstring& s, COLORREF col) {
    g_log.push_back(LogLine{s, col});
    if (g_log.size() > 600) g_log.erase(g_log.begin(), g_log.begin() + (g_log.size()-600));
    if (g_logScroll > 0) g_logScroll++;
    InvalidateRect(g_hwnd, nullptr, FALSE);
}

static std::wstring Decode(const std::string& s) {
    if (s.empty()) return L"";
    // UTF-16LE with BOM
    if (s.size() >= 2 && (unsigned char)s[0] == 0xFF && (unsigned char)s[1] == 0xFE) {
        std::wstring w;
        for (size_t i = 2; i + 1 < s.size(); i += 2)
            w.push_back((wchar_t)((unsigned char)s[i] | ((unsigned char)s[i+1] << 8)));
        return w;
    }
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    if (n <= 0) return std::wstring(s.begin(), s.end());
    std::wstring w((size_t)n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

static void DrawGlyph(HDC dc, int x, int y) {
    for (int i = 0; i < 3; i++) {
        int gx = x + i*22;
        Frame(dc, gx, y, 14, 14, Pal::accent);
        Fill(dc, gx+4, y+4, 6, 6, Pal::accent);
    }
    Line(dc, x+14, y+7, x+22, y+7, Pal::accent);
    Line(dc, x+36, y+7, x+44, y+7, Pal::accent);
}
static void DrawPanel(HDC dc, const RECT& r) {
    Fill(dc, r.left, r.top, r.right-r.left, r.bottom-r.top, Pal::panel);
    Frame(dc, r.left, r.top, r.right-r.left, r.bottom-r.top, Pal::line);
}
static void PaintAll(HDC dc) {
    Fill(dc, 0, 0, CW, CH, Pal::bg);

    // header
    Fill(dc, 0, 0, CW, HEADER_H, Pal::panel);
    Fill(dc, 0, 0, CW, 3, Pal::accent);
    Frame(dc, 0, 0, CW, HEADER_H, Pal::line);
    DrawTxt(dc, L"MAC IDENTITY SPOOFER", 20, 9, 500, 36, g_fTitle, Pal::text);
    DrawTxt(dc, L"Change the MAC address of a network adapter", 21, 46, 600, 16, g_fSmall, Pal::dim);
    DrawGlyph(dc, 968, 26);

    DrawPanel(dc, LeftPanel());
    DrawPanel(dc, RightPanel());

    RECT lp = LeftPanel();
    DrawTxt(dc, L"NETWORK ADAPTERS", lp.left+14, lp.top+10, 220, 22, g_fLabel, Pal::text);
    RECT rr = RefreshRect();
    Fill(dc, rr.left, rr.top, rr.right-rr.left, rr.bottom-rr.top,
         g_overRefresh && !g_busy ? Pal::hover : Pal::panel2);
    Frame(dc, rr.left, rr.top, rr.right-rr.left, rr.bottom-rr.top,
         g_overRefresh && !g_busy ? Pal::cyan : Pal::line);
    DrawTxt(dc, L"REFRESH", rr.left, rr.top, rr.right-rr.left, rr.bottom-rr.top,
            g_fSmall, Pal::cyan, DT_CENTER|DT_VCENTER|DT_SINGLELINE);

    RECT lr = ListRect();
    Fill(dc, lr.left, lr.top, lr.right-lr.left, lr.bottom-lr.top, Pal::panel2);
    Frame(dc, lr.left, lr.top, lr.right-lr.left, lr.bottom-lr.top, Pal::line);
    int visibleRows = (lr.bottom - lr.top) / ROW_H;
    int maxScroll = std::max(0, (int)g_adapters.size() - visibleRows);
    g_listScroll = std::min(g_listScroll, maxScroll);
    for (int i = g_listScroll; i < (int)g_adapters.size() && i < g_listScroll + visibleRows; i++) {
        const Adapter& a = g_adapters[i];
        int y = lr.top + (i - g_listScroll) * ROW_H;
        bool sel = (i == g_sel);
        Fill(dc, lr.left+1, y, lr.right-lr.left-2, ROW_H, sel ? C(35,51,66) : Pal::panel2);
        if (sel) Fill(dc, lr.left+1, y, 3, ROW_H, Pal::accent);
        std::wstring line1 = a.name;
        DrawTxt(dc, line1.c_str(), lr.left+11, y+5, lr.right-lr.left-24, 19, g_fBody,
                sel ? Pal::text : Pal::text);
        // status badge
        int badgeW = 62;
        int bx = lr.right - badgeW - 10;
        COLORREF badgeCol = a.disabled ? Pal::amber :
                            (a.status == L"Up" ? Pal::accent : Pal::dim);
        Frame(dc, bx, y+7, badgeW, 16, badgeCol);
        std::wstring st = a.status;
        if (st.size() > 9) st = st.substr(0,9);
        DrawTxt(dc, st.c_str(), bx, y+7, badgeW, 16, g_fSmall, badgeCol, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        std::wstring line2 = a.desc;
        if (line2.size() > 40) line2 = line2.substr(0,39) + L"...";
        DrawTxt(dc, line2.c_str(), lr.left+11, y+25, lr.right-lr.left-24, 15, g_fSmall, Pal::dim);
        Line(dc, lr.left+1, y+ROW_H-1, lr.right-1, y+ROW_H-1, Pal::line);
    }
    if (g_adapters.empty()) {
        DrawTxt(dc, L"No adapters detected", lr.left, lr.top, lr.right-lr.left, lr.bottom-lr.top,
                g_fSmall, Pal::dim, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    }

    int dy = lp.top + 322;
    Frame(dc, lp.left+10, dy-6, lp.right-lp.left-20, 1, Pal::line);
    DrawTxt(dc, L"SELECTED ADAPTER", lp.left+14, dy, 200, 18, g_fLabel, Pal::dim);

    const Adapter* sel = (g_sel >= 0 && g_sel < (int)g_adapters.size()) ? &g_adapters[g_sel] : nullptr;
    int fy = dy + 22;
    DrawTxt(dc, L"MAC", lp.left+14, fy, 90, 18, g_fSmall, Pal::dim);
    DrawTxt(dc, sel ? sel->mac.c_str() : L"--", lp.left+104, fy, 250, 18, g_fSmall, Pal::text);
    fy += 20;
    DrawTxt(dc, L"STATUS", lp.left+14, fy, 90, 18, g_fSmall, Pal::dim);
    DrawTxt(dc, sel ? sel->status.c_str() : L"--", lp.left+104, fy, 250, 18, g_fSmall,
            sel && sel->status == L"Up" ? Pal::accent : Pal::text);
    fy += 20;
    DrawTxt(dc, L"DEVICE ID", lp.left+14, fy, 90, 18, g_fSmall, Pal::dim);
    std::wstring id = sel ? sel->pnpid.substr(0, std::min<size_t>(sel->pnpid.size(), 22)) + L"..." : L"--";
    DrawTxt(dc, id.c_str(), lp.left+104, fy, 250, 18, g_fSmall, Pal::text);

    RECT me = MacEditRect();
    DrawTxt(dc, L"CUSTOM MAC", lp.left+14, me.top, 90, me.bottom-me.top, g_fSmall, Pal::dim);
    Fill(dc, me.left, me.top, me.right-me.left, me.bottom-me.top, Pal::panel2);
    Frame(dc, me.left, me.top, me.right-me.left, me.bottom-me.top,
          g_macEditing ? Pal::accent : Pal::line);
    std::wstring shown = g_macInput.empty() ? L"(auto-generated LAA)" : g_macInput;
    DrawTxt(dc, shown.c_str(), me.left+8, me.top, me.right-me.left-16, me.bottom-me.top,
            g_fSmall, g_macInput.empty() ? Pal::dim : Pal::text);
    if (g_macEditing && g_caretOn && !g_busy) {
        int cw = 7;
        int cx = me.left + 8;
        // measure text width
        HDC tmp = GetDC(g_hwnd);
        HFONT old = (HFONT)SelectObject(tmp, g_fSmall);
        SIZE sz; GetTextExtentPoint32W(tmp, shown.c_str(), (int)shown.size(), &sz);
        SelectObject(tmp, old); ReleaseDC(g_hwnd, tmp);
        cx += sz.cx;
        if (cx + cw > me.right - 6) cx = me.right - 6 - cw;
        Fill(dc, cx, me.top+6, cw, me.bottom-me.top-12, Pal::accent);
    }

    RECT rp = RightPanel();
    DrawTxt(dc, L"ACTIVITY LOG", rp.left+14, rp.top+10, 260, 22, g_fLabel, Pal::text);
    RECT logr = LogRect();
    Fill(dc, logr.left, logr.top, logr.right-logr.left, logr.bottom-logr.top, C(11,14,19));
    Frame(dc, logr.left, logr.top, logr.right-logr.left, logr.bottom-logr.top, Pal::line);
    int vlines = (logr.bottom - logr.top - 12) / LOG_LINE_H;
    int total = (int)g_log.size();
    int maxLs = std::max(0, total - vlines);
    g_logScroll = std::min(g_logScroll, maxLs);
    int start = total - vlines - g_logScroll;
    if (start < 0) start = 0;
    for (int i = start; i < total; i++) {
        int y = logr.top + 6 + (i - start) * LOG_LINE_H;
        if (y + LOG_LINE_H > logr.bottom) break;
        DrawTxt(dc, g_log[i].text.c_str(), logr.left+10, y, logr.right-logr.left-22,
                LOG_LINE_H, g_fMono, g_log[i].color);
    }
    if (total == 0) {
        DrawTxt(dc, L"Ready. Select an adapter and choose an action.", logr.left+10, logr.top,
                logr.right-logr.left-22, logr.bottom-logr.top, g_fSmall, Pal::dim,
                DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    }
    if (maxLs > 0) {
        int trackX = logr.right - 4;
        int trackY = logr.top + 4;
        int trackH = logr.bottom - logr.top - 8;
        Fill(dc, trackX, trackY, 3, trackH, Pal::line);
        int thumbH = std::max(24, trackH * vlines / total);
        int thumbY = trackY + (trackH - thumbH) * (maxLs - g_logScroll) / maxLs;
        Fill(dc, trackX, thumbY, 3, thumbH, Pal::accent);
    }

    for (int i = 0; i < NB; i++) {
        RECT b = ButtonRect(i);
        bool hov = (g_hover == i);
        Fill(dc, b.left, b.top, b.right-b.left, b.bottom-b.top,
             g_busy ? Pal::panel : (hov ? Pal::hover : Pal::panel2));
        Frame(dc, b.left, b.top, b.right-b.left, b.bottom-b.top,
              hov && !g_busy ? BTN_ACCENT[i] : Pal::line);
        Fill(dc, b.left, b.top, 3, b.bottom-b.top,
             g_busy ? Pal::line : BTN_ACCENT[i]);
        DrawTxt(dc, BTN_LABELS[i], b.left, b.top, b.right-b.left, b.bottom-b.top,
                g_fBtn, g_busy ? Pal::dim : (hov ? BTN_ACCENT[i] : Pal::text),
                DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    }

    RECT sr = StatusRect();
    Fill(dc, sr.left, sr.top, sr.right-sr.left, sr.bottom-sr.top, Pal::panel);
    Frame(dc, sr.left, sr.top, sr.right-sr.left, sr.bottom-sr.top, Pal::line);
    Fill(dc, sr.left+12, sr.top+11, 8, 8,
         g_busy ? Pal::amber : g_statusColor);
    DrawTxt(dc, g_status.c_str(), sr.left+30, sr.top, 500, sr.bottom-sr.top,
            g_fSmall, g_busy ? Pal::amber : g_statusColor);
    DrawTxt(dc, L"ADMIN", sr.right-260, sr.top, 248, sr.bottom-sr.top,
            g_fSmall, Pal::dim, DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
}

struct RunCtx {
    int mode;                 // 0=list 1=spoof 2=restore 3=revive 4=stealth
    std::wstring adapter, mac, ps;
    HWND hwnd;
};

static void SplitAdapterLine(const std::wstring& w, std::vector<Adapter>& out) {
    std::wstringstream ss(w);
    std::vector<std::wstring> f;
    std::wstring wp;
    while (std::getline(ss, wp, L'|')) f.push_back(wp);
    if (f.size() < 8) return;
    Adapter a;
    a.name = f[1]; a.desc = f[2]; a.mac = f[3]; a.status = f[4];
    a.guid = f[5]; a.pnpid = f[6]; a.disabled = (f[7] == L"True");
    out.push_back(a);
}

static void ProcessLine(RunCtx* c, const std::string& raw, std::vector<Adapter>& out) {
    std::wstring w = Decode(raw);
    if (w.empty()) return;
    if (w[0] == 0xFEFF) w.erase(0,1);
    if (c->mode == 0 && w.compare(0, 8, L"ADAPTER|") == 0) {
        SplitAdapterLine(w, out);
        return;
    }
    if (c->mode == 0 && (w == L"END" || w.compare(0, 4, L"END") == 0)) return;
    COLORREF col = ColorFor(w);
    wchar_t* p = new wchar_t[w.size()+1];
    std::memcpy(p, w.c_str(), (w.size()+1) * sizeof(wchar_t));
    PostMessage(c->hwnd, WM_LOG, (WPARAM)col, (LPARAM)p);
}

static DWORD WINAPI RunThread(LPVOID pv) {
    RunCtx* c = (RunCtx*)pv;
    std::wstring cmd = L"powershell.exe -NoProfile -ExecutionPolicy Bypass -File \"" + c->ps + L"\" -Headless";
    if (c->mode == 0) {
        cmd += L" -ListAdapters";
    } else if (c->mode == 1 || c->mode == 4) {
        cmd += L" -AdapterName \"" + c->adapter + L"\" -Spoof";
        if (!c->mac.empty()) cmd += L" -Mac \"" + c->mac + L"\"";
        if (c->mode == 4) cmd += L" -RandomizeHostname";
    } else if (c->mode == 2) {
        cmd += L" -AdapterName \"" + c->adapter + L"\" -Restore";
    } else if (c->mode == 3) {
        cmd += L" -AdapterName \"" + c->adapter + L"\" -Revive";
    }

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa); sa.bInheritHandle = TRUE;
    HANDLE hRead = nullptr, hWrite = nullptr;
    CreatePipe(&hRead, &hWrite, &sa, 0);
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);
    HANDLE hNul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ|FILE_SHARE_WRITE,
                              nullptr, OPEN_EXISTING, 0, nullptr);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = hWrite;
    si.hStdError = hWrite;
    si.hStdInput = hNul;
    PROCESS_INFORMATION pi{};

    std::wstring mutableCmd = cmd;
    std::vector<Adapter> found;
    BOOL ok = CreateProcessW(nullptr, &mutableCmd[0], nullptr, nullptr, TRUE,
                             CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    if (ok) {
        CloseHandle(hWrite);
        CloseHandle(hNul);
        char buf[4096];
        std::string acc;
        for (;;) {
            DWORD n = 0;
            if (!ReadFile(hRead, buf, sizeof(buf), &n, nullptr) || n == 0) break;
            acc.append(buf, n);
            size_t pos;
            while ((pos = acc.find('\n')) != std::string::npos) {
                std::string line = acc.substr(0, pos);
                acc.erase(0, pos+1);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                ProcessLine(c, line, found);
            }
        }
        if (!acc.empty()) ProcessLine(c, acc, found);
        CloseHandle(hRead);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    } else {
        CloseHandle(hWrite);
        CloseHandle(hRead);
        CloseHandle(hNul);
        wchar_t* p = new wchar_t[64];
        wcscpy_s(p, 64, L"[ERROR] Failed to launch PowerShell engine.");
        PostMessage(c->hwnd, WM_LOG, (WPARAM)Pal::red, (LPARAM)p);
    }

    if (c->mode == 0) {
        std::vector<Adapter>* v = new std::vector<Adapter>(found);
        PostMessage(c->hwnd, WM_ADAPTERS, 0, (LPARAM)v);
    }
    PostMessage(c->hwnd, WM_DONE, 0, 0);
    delete c;
    return 0;
}

static void Spawn(int mode) {
    if (g_psPath.empty() || !PathFileExistsW(g_psPath.c_str())) {
        AppendLog(L"[ERROR] Spoof-Mac.ps1 not found next to the executable.", Pal::red);
        return;
    }
    if (mode != 0 && (g_sel < 0 || g_sel >= (int)g_adapters.size())) {
        AppendLog(L"[ERROR] Select a network adapter first.", Pal::red);
        return;
    }
    RunCtx* c = new RunCtx();
    c->mode = mode;
    c->hwnd = g_hwnd;
    c->ps = g_psPath;
    if (mode != 0) {
        c->adapter = g_adapters[g_sel].name;
        c->mac = g_macInput;
    }
    g_busy = true;
    g_lastMode = mode;
    switch (mode) {
        case 0: g_status = L"SCANNING ADAPTERS"; break;
        case 1: g_status = L"SPOOFING MAC ADDRESS"; break;
        case 2: g_status = L"RESTORING FACTORY MAC"; break;
        case 3: g_status = L"REVIVING ADAPTER"; break;
        case 4: g_status = L"FULL STEALTH MODE"; break;
    }
    g_statusColor = Pal::amber;
    InvalidateRect(g_hwnd, nullptr, FALSE);

    HANDLE h = CreateThread(nullptr, 0, RunThread, c, 0, nullptr);
    if (h) CloseHandle(h);
    else {
        g_busy = false;
        g_status = L"READY";
        AppendLog(L"[ERROR] Could not start worker thread.", Pal::red);
        delete c;
        InvalidateRect(g_hwnd, nullptr, FALSE);
    }
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        g_hwnd = hwnd;
        InitFonts();
        int pref = DWMWCP_DONOTROUND;
        DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof(pref));
        BOOL dark = TRUE;
        DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
        COLORREF cap = Pal::panel, capText = Pal::text;
        DwmSetWindowAttribute(hwnd, DWMWA_CAPTION_COLOR, &cap, sizeof(cap));
        DwmSetWindowAttribute(hwnd, DWMWA_TEXT_COLOR, &capText, sizeof(capText));
        SetTimer(hwnd, 1, 500, nullptr);
        wchar_t exe[MAX_PATH];
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        std::wstring p(exe);
        size_t slash = p.find_last_of(L"\\/");
        g_psPath = (slash == std::wstring::npos ? std::wstring(L"") : p.substr(0, slash+1)) + L"Spoof-Mac.ps1";
        Spawn(0);
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);
        HDC mem = CreateCompatibleDC(hdc);
        HBITMAP bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
        HGDIOBJ old = SelectObject(mem, bmp);
        PaintAll(mem);
        BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_TIMER: {
        g_caretOn = !g_caretOn;
        if (g_macEditing) { RECT rc = MacEditRect(); InvalidateRect(hwnd, &rc, FALSE); }
        return 0;
    }
    case WM_MOUSEMOVE: {
        int x = LOWORD(lParam), y = HIWORD(lParam);
        int h = HitButton(x, y);
        bool overRefresh = In(RefreshRect(), x, y);
        bool overMac = In(MacEditRect(), x, y);
        if (h != g_hover || overRefresh != g_overRefresh) {
            g_hover = h;
            g_overRefresh = overRefresh;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        (void)overMac;
        if (h >= 0 || overRefresh) SetCursor(LoadCursor(nullptr, IDC_HAND));
        else if (overMac) SetCursor(LoadCursor(nullptr, IDC_IBEAM));
        else SetCursor(LoadCursor(nullptr, IDC_ARROW));
        return 0;
    }
    case WM_LBUTTONDOWN: {
        int x = LOWORD(lParam), y = HIWORD(lParam);
        if (In(RefreshRect(), x, y)) {
            if (!g_busy) Spawn(0);
            return 0;
        }
        int b = HitButton(x, y);
        if (b >= 0) {
            if (g_busy) return 0;
            if (b == NB-1) { g_log.clear(); g_logScroll = 0; g_macEditing = false; InvalidateRect(hwnd, nullptr, FALSE); return 0; }
            Spawn(b+1);
            return 0;
        }
        RECT lr = ListRect();
        if (In(lr, x, y)) {
            int row = (y - lr.top) / ROW_H + g_listScroll;
            if (row >= 0 && row < (int)g_adapters.size()) {
                g_sel = row;
                g_macEditing = false;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        if (In(MacEditRect(), x, y) && !g_busy) {
            g_macEditing = true;
            g_caretOn = true;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        g_macEditing = false;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_MOUSEWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        int steps = (delta > 0) ? -1 : 1;
        POINT pt{ (short)LOWORD(lParam), (short)HIWORD(lParam) };
        ScreenToClient(hwnd, &pt);
        if (In(ListRect(), pt.x, pt.y)) {
            g_listScroll = std::max(0, g_listScroll + steps);
            InvalidateRect(hwnd, nullptr, FALSE);
        } else if (In(LogRect(), pt.x, pt.y)) {
            g_logScroll = std::max(0, g_logScroll + steps);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_CHAR: {
        if (!g_macEditing || g_busy) return 0;
        if (wParam == VK_BACK) {
            if (!g_macInput.empty()) g_macInput.pop_back();
        } else if (wParam == VK_ESCAPE) {
            g_macEditing = false;
        } else if (wParam == VK_RETURN) {
            g_macEditing = false;
        } else if (wParam >= 0x20 && wParam < 0x7f && g_macInput.size() < 17) {
            wchar_t ch = (wchar_t)wParam;
            if ((ch >= L'0' && ch <= L'9') || (ch >= L'a' && ch <= L'f') ||
                (ch >= L'A' && ch <= L'F') || ch == L'-' || ch == L':')
                g_macInput.push_back(ch);
        }
        { RECT rc = MacEditRect(); InvalidateRect(hwnd, &rc, FALSE); }
        return 0;
    }
    case WM_KEYDOWN: {
        if (wParam == VK_UP || wParam == VK_DOWN) {
            int dir = (wParam == VK_UP) ? -1 : 1;
            int n = (int)g_adapters.size();
            if (n > 0) {
                g_sel = (g_sel + dir + n) % n;
                int visibleRows = (ListRect().bottom - ListRect().top) / ROW_H;
                if (g_sel < g_listScroll) g_listScroll = g_sel;
                if (g_sel >= g_listScroll + visibleRows) g_listScroll = g_sel - visibleRows + 1;
                g_macEditing = false;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        return 0;
    }
    case WM_LOG: {
        wchar_t* p = (wchar_t*)lParam;
        AppendLog(p, (COLORREF)wParam);
        delete[] p;
        return 0;
    }
    case WM_ADAPTERS: {
        std::vector<Adapter>* v = (std::vector<Adapter>*)lParam;
        g_adapters.swap(*v);
        delete v;
        g_listScroll = 0;
        if (!g_adapters.empty()) {
            g_sel = 0;
            wchar_t msg[96];
            swprintf_s(msg, 96, L"[*] Detected %d compatible network adapters.", (int)g_adapters.size());
            AppendLog(msg, Pal::cyan);
        } else {
            g_sel = -1;
            AppendLog(L"[!] No compatible network adapters detected.", Pal::amber);
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_DONE: {
        g_busy = false;
        if (g_lastMode == 0) {
            g_status = g_adapters.empty() ? L"NO ADAPTERS FOUND" : L"READY - ADAPTERS LOADED";
            g_statusColor = g_adapters.empty() ? Pal::red : Pal::accent;
        } else {
            g_status = L"OPERATION COMPLETE";
            g_statusColor = Pal::accent;
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_CLOSE:
        PostQuitMessage(0);
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, 1);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int nCmdShow) {
    g_hInst = hInst;
    const wchar_t* CLASS = L"MacSpoofMain";
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hIcon = (HICON)LoadImageW(hInst, L"APPICON", IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR);
    if (!wc.hIcon) wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
    wc.lpszClassName = CLASS;
    RegisterClassExW(&wc);

    RECT wr{0,0,CW,CH};
    AdjustWindowRectEx(&wr, WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX, FALSE, 0);
    int w = wr.right - wr.left, h = wr.bottom - wr.top;
    int sx = (GetSystemMetrics(SM_CXSCREEN) - w) / 2;
    int sy = (GetSystemMetrics(SM_CYSCREEN) - h) / 2;
    HWND hwnd = CreateWindowExW(0, CLASS, L"MAC Identity Spoofer",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        sx, sy, w, h, nullptr, nullptr, hInst, nullptr);
    if (!hwnd) return 1;
    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}
