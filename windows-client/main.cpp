#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsock2.h>
#include <commctrl.h>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <sstream>
#include <algorithm>
#include "WsClient.h"
#include "Protocol.h"

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "comctl32.lib")

// ── Control IDs ───────────────────────────────────────────────────────────
#define ID_EDIT_HOST    101
#define ID_EDIT_PORT    102
#define ID_BTN_CONNECT  103
#define ID_LBL_STATUS   104
#define ID_LBL_TIMER    105
#define ID_LBL_SUB      106
#define ID_LBL_COST     107
#define ID_LBL_USER     108
#define ID_LBL_PKG      109
#define ID_LBL_STATION  110
#define ID_BTN_CHAT     111
#define ID_PROGRESS     112
#define ID_LBL_CONN     113
#define ID_CHAT_VIEW    201
#define ID_CHAT_ENTRY   202
#define ID_CHAT_SEND    203

// ── App state ─────────────────────────────────────────────────────────────
static WsClient* g_client    = nullptr;
static HWND      g_hwnd      = nullptr;
static HWND      g_chat_hwnd = nullptr;

static bool   g_connected      = false;
static bool   g_session_active = false;
static bool   g_locked         = true;
static int    g_elapsed        = 0;
static int    g_remaining      = 0;
static int    g_duration       = 0;
static double g_cost           = 0.0;
static char   g_package[128]   = "";
static char   g_username[128]  = "";
static char   g_station[128]   = "Client";

// Panel states
static bool g_showing_main = false;

// Controls on connect panel
static HWND hEditHost, hEditPort, hBtnConnect, hLblConnStatus;
// Controls on main panel
static HWND hLblTimer, hLblSub, hLblCost, hLblUser, hLblPkg,
            hLblStation, hBtnChat, hProgress, hLblConnBar;
// Lock overlay
static HWND hLockOverlay = nullptr;

// Fullscreen / always-on-top state (restored when unlocked)
static bool  g_style_saved = false;
static LONG  g_prev_style  = 0;
static RECT  g_prev_rect   = {};
static bool  g_in_apply_lock = false;

// Global keyboard hook used to swallow Start/Alt+Tab/Alt+F4/Ctrl+Esc while locked
static HHOOK g_kb_hook = nullptr;

// Fonts
static HFONT g_fontHuge   = nullptr;
static HFONT g_fontBig    = nullptr;
static HFONT g_fontNormal = nullptr;
static HFONT g_fontMono   = nullptr;

// Forward declarations
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK ChatProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK LockProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK LowLevelKeyboardProc(int, WPARAM, LPARAM);
void ShowConnectPanel(HWND hwnd, bool show);
void ShowMainPanel(HWND hwnd);
void UpdateTimerDisplay();
void ApplyLock(bool locked);
void OpenChatWindow();
void HandleMsg(const Protocol::Message& msg);
void AppendChat(const std::string& line);

// ── WinMain ───────────────────────────────────────────────────────────────

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int nCmdShow) {
    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_PROGRESS_CLASS };
    InitCommonControlsEx(&icc);

    // Create fonts
    g_fontHuge   = CreateFont(-72, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                     DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                     CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, "Courier New");
    g_fontBig    = CreateFont(-24, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                     DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                     CLEARTYPE_QUALITY, VARIABLE_PITCH | FF_SWISS, "Segoe UI");
    g_fontNormal = CreateFont(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                     DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                     CLEARTYPE_QUALITY, VARIABLE_PITCH | FF_SWISS, "Segoe UI");
    g_fontMono   = CreateFont(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                     DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                     CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, "Courier New");

    // Register window class
    WNDCLASSEX wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = "CafeBillClient";
    wc.hIcon         = LoadIcon(nullptr, IDI_APPLICATION);
    RegisterClassEx(&wc);

    // Lock overlay class
    WNDCLASSEX lc{};
    lc.cbSize        = sizeof(lc);
    lc.lpfnWndProc   = LockProc;
    lc.hInstance     = hInst;
    lc.hbrBackground = CreateSolidBrush(RGB(20, 20, 20));
    lc.lpszClassName = "CafeBillLock";
    RegisterClassEx(&lc);

    // Chat window class
    WNDCLASSEX cc{};
    cc.cbSize        = sizeof(cc);
    cc.lpfnWndProc   = ChatProc;
    cc.hInstance     = hInst;
    cc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    cc.lpszClassName = "CafeBillChat";
    RegisterClassEx(&cc);

    g_hwnd = CreateWindowEx(0, "CafeBillClient", "CaféBill — Client",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 800, 600,
        nullptr, nullptr, hInst, nullptr);

    ShowWindow(g_hwnd, nCmdShow);
    UpdateWindow(g_hwnd);

    g_client = new WsClient();

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    delete g_client;
    DeleteObject(g_fontHuge);
    DeleteObject(g_fontBig);
    DeleteObject(g_fontNormal);
    DeleteObject(g_fontMono);
    return (int)msg.wParam;
}

// ── Helper: make label ────────────────────────────────────────────────────

static HWND MkLabel(HWND parent, const char* text, int x, int y, int w, int h,
                    HFONT font = nullptr, DWORD style = SS_CENTER) {
    HWND lbl = CreateWindow("STATIC", text,
        WS_CHILD | WS_VISIBLE | style, x, y, w, h,
        parent, nullptr, GetModuleHandle(nullptr), nullptr);
    if (font) SendMessage(lbl, WM_SETFONT, (WPARAM)font, TRUE);
    return lbl;
}

static HWND MkEdit(HWND parent, const char* text, int id, int x, int y,
                   int w, int h, DWORD exstyle = 0) {
    HWND e = CreateWindowEx(exstyle, "EDIT", text,
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, x, y, w, h,
        parent, (HMENU)(UINT_PTR)id, GetModuleHandle(nullptr), nullptr);
    SendMessage(e, WM_SETFONT, (WPARAM)g_fontNormal, TRUE);
    return e;
}

static HWND MkBtn(HWND parent, const char* text, int id, int x, int y, int w, int h) {
    HWND b = CreateWindow("BUTTON", text,
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, x, y, w, h,
        parent, (HMENU)(UINT_PTR)id, GetModuleHandle(nullptr), nullptr);
    SendMessage(b, WM_SETFONT, (WPARAM)g_fontNormal, TRUE);
    return b;
}

// ── Show panels ───────────────────────────────────────────────────────────

void ShowConnectPanel(HWND hwnd, bool show) {
    if (show) {
        // Reset
        g_showing_main = false;

        // Destroy old controls
        HWND child = GetWindow(hwnd, GW_CHILD);
        while (child) {
            HWND next = GetWindow(child, GW_HWNDNEXT);
            if (child != hLockOverlay) DestroyWindow(child);
            child = next;
        }
        hLockOverlay = nullptr;

        RECT rc; GetClientRect(hwnd, &rc);
        int W = rc.right, H = rc.bottom;
        int cx = W / 2, cy = H / 2;

        // Dark background via WM_ERASEBKGND (already black brush on class)
        SetWindowText(hwnd, "CaféBill — Client");

        // Logo/title
        HWND lbl = MkLabel(hwnd, "☕ CafeBill", cx-200, cy-160, 400, 50, g_fontBig);
        SetWindowLongPtr(lbl, GWLP_ID, ID_LBL_STATUS);

        MkLabel(hwnd, "Server IP Address:", cx-200, cy-100, 140, 24, g_fontNormal, SS_RIGHT);
        hEditHost = MkEdit(hwnd, "127.0.0.1", ID_EDIT_HOST, cx-50, cy-104, 180, 26);

        MkLabel(hwnd, "Port:", cx-200, cy-66, 140, 24, g_fontNormal, SS_RIGHT);
        hEditPort = MkEdit(hwnd, "12345", ID_EDIT_PORT, cx-50, cy-70, 80, 26);

        hBtnConnect = MkBtn(hwnd, "Connect to Server", ID_BTN_CONNECT, cx-100, cy-28, 200, 36);

        hLblConnStatus = MkLabel(hwnd, "", cx-200, cy+20, 400, 24, g_fontNormal);

        // Set text colors by subclassing not needed — use WM_CTLCOLORSTATIC
    } else {
        // Hide connect panel
    }
}

void ShowMainPanel(HWND hwnd) {
    g_showing_main = true;

    // Destroy connect panel controls
    HWND child = GetWindow(hwnd, GW_CHILD);
    while (child) {
        HWND next = GetWindow(child, GW_HWNDNEXT);
        DestroyWindow(child);
        child = next;
    }

    RECT rc; GetClientRect(hwnd, &rc);
    int W = rc.right, H = rc.bottom;
    int cx = W / 2;

    // Station label (top-left)
    hLblStation = MkLabel(hwnd, "Station: —", 16, 10, 300, 22, g_fontNormal, SS_LEFT);
    hLblConnBar = MkLabel(hwnd, "● Connected", W-200, 10, 190, 22, g_fontNormal, SS_RIGHT);

    // Large timer
    hLblTimer = MkLabel(hwnd, "00:00:00", 0, H/2-140, W, 90, g_fontHuge);

    // Sub-label (Elapsed / Remaining)
    hLblSub = MkLabel(hwnd, "Waiting for session...", 0, H/2-50, W, 28, g_fontBig);

    // Progress bar
    hProgress = CreateWindowEx(0, PROGRESS_CLASS, nullptr,
        WS_CHILD | PBS_SMOOTH, cx-200, H/2-10, 400, 20,
        hwnd, (HMENU)ID_PROGRESS, GetModuleHandle(nullptr), nullptr);
    ShowWindow(hProgress, SW_HIDE);

    // Cost
    hLblCost = MkLabel(hwnd, "Rp 0", 0, H/2+20, W, 48, g_fontBig);

    // Info row
    hLblUser = MkLabel(hwnd, "User: —", cx-200, H/2+80, 190, 22, g_fontNormal, SS_CENTER);
    hLblPkg  = MkLabel(hwnd, "Package: —", cx+10,  H/2+80, 190, 22, g_fontNormal, SS_CENTER);

    // Chat button
    hBtnChat = MkBtn(hwnd, "Chat with Admin", ID_BTN_CHAT, cx-90, H/2+120, 180, 36);
    SendMessage(hBtnChat, WM_SETFONT, (WPARAM)g_fontBig, TRUE);

    // Lock overlay (child window, covers everything)
    hLockOverlay = CreateWindowEx(WS_EX_LAYERED,
        "CafeBillLock", nullptr,
        WS_CHILD | WS_VISIBLE, 0, 0, W, H,
        hwnd, nullptr, GetModuleHandle(nullptr), nullptr);
    // Semi-transparent dark overlay
    SetLayeredWindowAttributes(hLockOverlay, 0, 230, LWA_ALPHA);

    ApplyLock(true);
}

// ── Lock overlay ──────────────────────────────────────────────────────────

void ApplyLock(bool locked) {
    g_locked = locked;
    if (g_in_apply_lock || !g_hwnd) return;
    g_in_apply_lock = true;

    if (locked) {
        if (hLockOverlay) {
            ShowWindow(hLockOverlay, SW_SHOW);
            BringWindowToTop(hLockOverlay);
        }

        // Remember the current windowed geometry exactly once so it can be
        // restored after the session unlocks.
        if (!g_style_saved) {
            g_prev_style = (LONG)GetWindowLongPtr(g_hwnd, GWL_STYLE);
            GetWindowRect(g_hwnd, &g_prev_rect);
            g_style_saved = true;
        }

        // Fullscreen + always-on-top across the monitor the window is on.
        MONITORINFO mi{ sizeof(mi) };
        GetMonitorInfo(MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTONEAREST), &mi);
        SetWindowLongPtr(g_hwnd, GWL_STYLE, WS_VISIBLE | WS_POPUP);
        SetWindowPos(g_hwnd, HWND_TOPMOST,
            mi.rcMonitor.left, mi.rcMonitor.top,
            mi.rcMonitor.right  - mi.rcMonitor.left,
            mi.rcMonitor.bottom - mi.rcMonitor.top,
            SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        SetForegroundWindow(g_hwnd);

        // Swallow Start / Alt+Tab / Alt+F4 / Ctrl+Esc / Win+Tab globally.
        if (!g_kb_hook) {
            g_kb_hook = SetWindowsHookExW(WH_KEYBOARD_LL,
                LowLevelKeyboardProc, GetModuleHandleW(nullptr), 0);
        }
    } else {
        if (hLockOverlay) ShowWindow(hLockOverlay, SW_HIDE);

        // Stop blocking system keys and restore the windowed geometry.
        if (g_kb_hook) {
            UnhookWindowsHookEx(g_kb_hook);
            g_kb_hook = nullptr;
        }
        if (g_style_saved) {
            SetWindowLongPtr(g_hwnd, GWL_STYLE, g_prev_style);
            SetWindowPos(g_hwnd, HWND_NOTOPMOST,
                g_prev_rect.left, g_prev_rect.top,
                g_prev_rect.right  - g_prev_rect.left,
                g_prev_rect.bottom - g_prev_rect.top,
                SWP_FRAMECHANGED | SWP_SHOWWINDOW);
            g_style_saved = false;
        }
        SetForegroundWindow(g_hwnd);
    }
    g_in_apply_lock = false;
}

// ── Keyboard hook (only active while locked) ───────────────────────────────

LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && g_locked && wParam != WM_SYSKEYUP) {
        auto* kb = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
        UINT vk = kb->vkCode;
        bool alt  = (GetAsyncKeyState(VK_MENU)   & 0x8000) != 0;
        bool ctrl = (GetAsyncKeyState(VK_CONTROL)& 0x8000) != 0;
        bool win  = (GetAsyncKeyState(VK_LWIN)   & 0x8000) != 0 ||
                    (GetAsyncKeyState(VK_RWIN)   & 0x8000) != 0;

        if (vk == VK_LWIN || vk == VK_RWIN) return 1;      // Start keys
        if (win && vk == VK_TAB)              return 1;      // Win+Tab
        if (alt && (vk == VK_TAB || vk == VK_F4 ||
                    vk == VK_ESCAPE))         return 1;      // Alt+Tab / Alt+F4 / Alt+Esc
        if (ctrl && (vk == VK_TAB || vk == VK_ESCAPE)) return 1; // Ctrl+Esc / Ctrl+Tab
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

// ── Update timer display ──────────────────────────────────────────────────

void UpdateTimerDisplay() {
    if (!g_session_active) return;

    char tbuf[16];
    if (g_duration > 0) {
        // Timed: show remaining
        int r = std::max(0, g_remaining);
        snprintf(tbuf, sizeof(tbuf), "%02d:%02d:%02d", r/3600, (r%3600)/60, r%60);
        SetWindowText(hLblSub, "Time Remaining");
        // Progress
        SendMessage(hProgress, PBM_SETRANGE, 0, MAKELPARAM(0, g_duration));
        SendMessage(hProgress, PBM_SETPOS, g_duration - r, 0);
        ShowWindow(hProgress, SW_SHOW);
    } else {
        // Hourly: show elapsed
        snprintf(tbuf, sizeof(tbuf), "%02d:%02d:%02d",
            g_elapsed/3600, (g_elapsed%3600)/60, g_elapsed%60);
        SetWindowText(hLblSub, "Time Elapsed");
        ShowWindow(hProgress, SW_HIDE);
    }
    SetWindowText(hLblTimer, tbuf);

    char cbuf[32];
    snprintf(cbuf, sizeof(cbuf), "Rp %.0f", g_cost);
    SetWindowText(hLblCost, cbuf);
}

// ── Handle protocol message ───────────────────────────────────────────────

void HandleMsg(const Protocol::Message& msg) {
    if (msg.type == Protocol::REGISTERED) {
        // Move to main screen
        char hostname[256] = "WinClient";
        DWORD sz = sizeof(hostname);
        GetComputerNameA(hostname, &sz);
        snprintf(g_station, sizeof(g_station), "Station: %s", hostname);

        ShowMainPanel(g_hwnd);
        SetWindowText(hLblStation, g_station);
        SetWindowText(hLblConnBar, "● Connected — waiting for session");
    }
    else if (msg.type == Protocol::SESSION_START) {
        g_session_active = true;
        g_elapsed    = 0;
        g_cost       = 0.0;

        if (msg.fields.count("package"))  strncpy(g_package,  msg.fields.at("package").c_str(),  127);
        if (msg.fields.count("username")) strncpy(g_username, msg.fields.at("username").c_str(), 127);
        g_duration = msg.fields.count("duration") ? std::stoi(msg.fields.at("duration")) : 0;

        char buf[128];
        snprintf(buf, sizeof(buf), "User: %s", g_username[0] ? g_username : "—");
        SetWindowText(hLblUser, buf);
        snprintf(buf, sizeof(buf), "Package: %s", g_package);
        SetWindowText(hLblPkg, buf);
        SetWindowText(hLblConnBar, "● Session Active");
        SetWindowText(hLblTimer, "00:00:00");
        SetWindowText(hLblCost, "Rp 0");
        SetWindowText(hLblSub, g_duration > 0 ? "Time Remaining" : "Time Elapsed");

        ApplyLock(false);
    }
    else if (msg.type == Protocol::SESSION_END) {
        g_session_active = false;
        ApplyLock(true);
        SetWindowText(hLblConnBar, "● Session Ended");
        SetWindowText(hLblSub, "Session finished — please pay");

        int elapsed = msg.fields.count("elapsed") ? std::stoi(msg.fields.at("elapsed")) : g_elapsed;
        double cost = msg.fields.count("cost")    ? std::stod(msg.fields.at("cost"))    : g_cost;

        char summary[256];
        snprintf(summary, sizeof(summary),
            "Session Ended\n\nDuration: %02d:%02d:%02d\nTotal Cost: Rp %.0f\n\nThank you for your visit!",
            elapsed/3600, (elapsed%3600)/60, elapsed%60, cost);
        MessageBox(g_hwnd, summary, "Session Summary", MB_OK | MB_ICONINFORMATION);
    }
    else if (msg.type == Protocol::TICK) {
        g_elapsed    = msg.fields.count("elapsed")   ? std::stoi(msg.fields.at("elapsed"))   : g_elapsed + 1;
        g_remaining  = msg.fields.count("remaining") ? std::stoi(msg.fields.at("remaining")) : g_remaining - 1;
        g_cost       = msg.fields.count("cost")      ? std::stod(msg.fields.at("cost"))      : g_cost;
        UpdateTimerDisplay();
    }
    else if (msg.type == Protocol::CHAT_SERVER) {
        std::string from = msg.fields.count("from") ? msg.fields.at("from") : "Admin";
        std::string text = msg.fields.count("text") ? msg.fields.at("text") : "";

        OpenChatWindow();

        time_t now = time(nullptr);
        struct tm* tm = localtime(&now);
        char ts[16]; strftime(ts, sizeof(ts), "%H:%M:%S", tm);
        std::string line = std::string("[") + ts + "] " + from + ": " + text + "\r\n";
        AppendChat(line);

        // Flash taskbar
        FlashWindow(g_hwnd, TRUE);
        SetWindowText(hBtnChat, "! New Message !");
    }
    else if (msg.type == Protocol::LOCK) {
        bool locked = msg.fields.count("locked") && msg.fields.at("locked") == "1";
        ApplyLock(locked);
    }
    else if (msg.type == Protocol::SERVER_MSG) {
        std::string text = msg.fields.count("text") ? msg.fields.at("text") : "";
        MessageBox(g_hwnd, text.c_str(), "Message from Admin", MB_OK | MB_ICONINFORMATION);
    }
}

// ── Chat window ───────────────────────────────────────────────────────────

void AppendChat(const std::string& line) {
    if (!g_chat_hwnd) return;
    HWND hView = GetDlgItem(g_chat_hwnd, ID_CHAT_VIEW);
    if (!hView) return;

    // Move to end and append
    int len = GetWindowTextLength(hView);
    SendMessage(hView, EM_SETSEL, len, len);
    SendMessage(hView, EM_REPLACESEL, FALSE, (LPARAM)line.c_str());
    SendMessage(hView, WM_VSCROLL, SB_BOTTOM, 0);
}

void OpenChatWindow() {
    if (g_chat_hwnd && IsWindow(g_chat_hwnd)) {
        SetForegroundWindow(g_chat_hwnd);
        return;
    }

    g_chat_hwnd = CreateWindow("CafeBillChat", "Chat with Admin",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 420, 520,
        g_hwnd, nullptr, GetModuleHandle(nullptr), nullptr);

    ShowWindow(g_chat_hwnd, SW_SHOW);
    UpdateWindow(g_chat_hwnd);
}

// ── Main window proc ──────────────────────────────────────────────────────

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        g_hwnd = hwnd;
        ShowConnectPanel(hwnd, true);
        return 0;

    case WM_SIZE:
        if (g_showing_main) ShowMainPanel(hwnd);
        else ShowConnectPanel(hwnd, true);
        return 0;

    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wp;
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(220, 220, 220));
        return (LRESULT)GetStockObject(BLACK_BRUSH);
    }

    case WM_CTLCOLOREDIT: {
        HDC hdc = (HDC)wp;
        SetBkColor(hdc, RGB(40, 40, 40));
        SetTextColor(hdc, RGB(220, 220, 220));
        static HBRUSH editBrush = CreateSolidBrush(RGB(40, 40, 40));
        return (LRESULT)editBrush;
    }

    case WM_CLOSE:
        if (g_locked) return 0;          // ignore close while locked
        break;

    case WM_SYSCOMMAND:
        if (g_locked && (wp & 0xFFF0) == SC_CLOSE) return 0;
        break;

    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
        if (g_locked && (wp == VK_TAB || wp == VK_F4 || wp == VK_ESCAPE)) return 0;
        break;

    case WM_KEYDOWN:
    case WM_KEYUP:
        if (g_locked && (wp == VK_LWIN || wp == VK_RWIN)) return 0;
        break;

    case WM_COMMAND:
        if (LOWORD(wp) == ID_BTN_CONNECT) {
            char host[256], ports[16];
            GetWindowTextA(hEditHost, host, sizeof(host));
            GetWindowTextA(hEditPort, ports, sizeof(ports));
            int port = ports[0] ? atoi(ports) : 12345;

            SetWindowText(hLblConnStatus, "Connecting...");

            if (!g_client->connect(host, port, hwnd)) {
                SetWindowText(hLblConnStatus,
                    "Connection failed. Check server IP and port.");
            }
        }
        else if (LOWORD(wp) == ID_BTN_CHAT) {
            OpenChatWindow();
            SetWindowText(hBtnChat, "Chat with Admin");
        }
        return 0;

    case WM_SOCKET_MSG: {
        auto* pm = reinterpret_cast<SocketMsg*>(lp);
        HandleMsg(pm->msg);
        delete pm;
        return 0;
    }

    case WM_SOCKET_DISC:
        g_connected = false;
        g_session_active = false;
        if (g_showing_main) {
            // Leave fullscreen/always-on-top so the user can reconnect.
            g_locked = false;
            ApplyLock(false);
            ShowConnectPanel(hwnd, true);
            SetWindowText(hLblConnStatus, "Disconnected from server.");
        }
        return 0;

    case WM_DESTROY:
        if (g_kb_hook) { UnhookWindowsHookEx(g_kb_hook); g_kb_hook = nullptr; }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

// ── Lock overlay proc ─────────────────────────────────────────────────────

LRESULT CALLBACK LockProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);
        SetBkMode(hdc, TRANSPARENT);

        // Draw lock icon and text
        SetTextColor(hdc, RGB(255, 255, 255));

        HFONT bigFont = CreateFont(-48, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, VARIABLE_PITCH | FF_SWISS, "Segoe UI");
        HFONT oldFont = (HFONT)SelectObject(hdc, bigFont);

        RECT textRc = rc;
        textRc.top = rc.top + (rc.bottom - rc.top) / 2 - 60;
        DrawText(hdc, "🔒  Workstation Locked", -1, &textRc,
            DT_CENTER | DT_SINGLELINE);

        SelectObject(hdc, g_fontNormal ? g_fontNormal : oldFont);
        DeleteObject(bigFont);
        SelectObject(hdc, g_fontNormal);

        RECT sub = rc;
        sub.top = rc.top + (rc.bottom - rc.top) / 2 + 20;
        SetTextColor(hdc, RGB(180, 180, 180));
        DrawText(hdc, "Please contact staff to start a session", -1, &sub,
            DT_CENTER | DT_SINGLELINE);

        SelectObject(hdc, oldFont);
        EndPaint(hwnd, &ps);
        return 0;
    }
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

// ── Chat window proc ──────────────────────────────────────────────────────

LRESULT CALLBACK ChatProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        RECT rc; GetClientRect(hwnd, &rc);
        int W = 420, H = 520;

        // Text view (multiline edit, read-only)
        HWND hView = CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT", "",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
            8, 8, W - 16, H - 80, hwnd, (HMENU)ID_CHAT_VIEW,
            GetModuleHandle(nullptr), nullptr);
        SendMessage(hView, WM_SETFONT, (WPARAM)g_fontMono, TRUE);

        // Input entry
        HWND hEntry = CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT", "",
            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
            8, H - 60, W - 100, 28, hwnd, (HMENU)ID_CHAT_ENTRY,
            GetModuleHandle(nullptr), nullptr);
        SendMessage(hEntry, WM_SETFONT, (WPARAM)g_fontNormal, TRUE);

        // Send button
        HWND hSend = CreateWindow("BUTTON", "Send",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            W - 88, H - 60, 80, 28, hwnd, (HMENU)ID_CHAT_SEND,
            GetModuleHandle(nullptr), nullptr);
        SendMessage(hSend, WM_SETFONT, (WPARAM)g_fontNormal, TRUE);

        return 0;
    }

    case WM_COMMAND:
        if (LOWORD(wp) == ID_CHAT_SEND ||
           (LOWORD(wp) == ID_CHAT_ENTRY && HIWORD(wp) == EN_CHANGE)) {
            if (LOWORD(wp) != ID_CHAT_SEND) break;

            HWND hEntry = GetDlgItem(hwnd, ID_CHAT_ENTRY);
            char text[512] = "";
            GetWindowTextA(hEntry, text, sizeof(text));
            if (!text[0]) break;

            // Append to view
            time_t now = time(nullptr);
            struct tm* tm = localtime(&now);
            char ts[16]; strftime(ts, sizeof(ts), "%H:%M:%S", tm);
            std::string line = std::string("[") + ts + "] Me: " + text + "\r\n";
            AppendChat(line);

            // Send to server
            if (g_client && g_client->isConnected())
                g_client->send(Protocol::encode(Protocol::CHAT_CLIENT, {{"text", text}}));

            SetWindowText(hEntry, "");
        }
        break;

    case WM_DESTROY:
        g_chat_hwnd = nullptr;
        return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}
