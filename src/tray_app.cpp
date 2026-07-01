#include "tray_app.h"
#include "codex_client.h"
#include "icon_renderer.h"

#include <windows.h>
#include <shellapi.h>
#include <thread>
#include <algorithm>

using namespace std::chrono;

// ---------------------------------------------------------------------------
// Menu IDs
// ---------------------------------------------------------------------------
enum {
    IDM_REFRESH = 1,
    IDM_COPY    = 2,
    IDM_EXIT    = 3,
    IDM_LANG_EN = 4,
    IDM_LANG_ZH = 5,
};

static constexpr const wchar_t* SETTINGS_KEY = L"Software\\CodexTrayGauge";

// ---------------------------------------------------------------------------
// s_instance for the static WndProc thunk
// ---------------------------------------------------------------------------
static TrayApp* g_app = nullptr;

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

TrayApp::TrayApp(HINSTANCE hInstance)
    : m_hInst(hInstance)
{
    loadSettings();
    g_app = this;
}

TrayApp::~TrayApp() {
    m_closing = true;
    if (m_refreshThread.joinable())
        m_refreshThread.join();
    if (m_hIcon) { DestroyIcon(m_hIcon); m_hIcon = nullptr; }
    if (m_hMenu) { DestroyMenu(m_hMenu); m_hMenu = nullptr; }
    g_app = nullptr;
}

void TrayApp::loadSettings() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, SETTINGS_KEY, 0, KEY_READ, &key) != ERROR_SUCCESS)
        return;

    wchar_t value[16] = {};
    DWORD type = REG_SZ;
    DWORD bytes = sizeof(value);
    if (RegQueryValueExW(key, L"Language", nullptr, &type,
                         reinterpret_cast<LPBYTE>(value), &bytes) == ERROR_SUCCESS &&
        type == REG_SZ) {
        if (wcscmp(value, L"zh") == 0)
            m_language = UiLanguage::Chinese;
        else
            m_language = UiLanguage::English;
    }
    RegCloseKey(key);
}

void TrayApp::saveSettings() {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, SETTINGS_KEY, 0, nullptr, 0,
                        KEY_WRITE, nullptr, &key, nullptr) != ERROR_SUCCESS)
        return;

    const wchar_t* value = m_language == UiLanguage::Chinese ? L"zh" : L"en";
    RegSetValueExW(key, L"Language", 0, REG_SZ,
                   reinterpret_cast<const BYTE*>(value),
                   static_cast<DWORD>((wcslen(value) + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
}

std::wstring TrayApp::tr(const wchar_t* en, const wchar_t* zh) const {
    return m_language == UiLanguage::Chinese ? zh : en;
}

// ---------------------------------------------------------------------------
// run — register class, create hidden window, pump messages
// ---------------------------------------------------------------------------

int TrayApp::run() {
    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = s_WndProc;
    wc.hInstance     = m_hInst;
    wc.lpszClassName = CLASS_NAME;
    if (!RegisterClassExW(&wc)) return 1;

    m_hwnd = CreateWindowExW(0, CLASS_NAME, L"", 0,
                             0, 0, 0, 0, nullptr, nullptr, m_hInst, this);
    if (!m_hwnd) return 1;

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

// ---------------------------------------------------------------------------
// Window proc
// ---------------------------------------------------------------------------

LRESULT CALLBACK TrayApp::s_WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    // Capture hwnd during WM_CREATE before m_hwnd is assigned in run()
    if (msg == WM_CREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        auto* app = static_cast<TrayApp*>(cs->lpCreateParams);
        g_app = app;
        app->m_hwnd = hwnd;
    }
    if (g_app) return g_app->wndProc(hwnd, msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT TrayApp::wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        createTrayIcon();
        m_timerId = SetTimer(hwnd, TIMER_MAIN, 1000, nullptr);  // 1 s tick
        PostMessageW(hwnd, WM_COMMAND, IDM_REFRESH, 0);          // first refresh
        break;

    case WM_TIMER:
        if (wp == TIMER_MAIN) {
            // Update tooltip every ~15 s so countdown is fresh
            static int tick = 0;
            if (++tick % 15 == 0)
                updateTooltip(m_quota.tooltipText(m_language));

            // Adaptive refresh
            auto now = steady_clock::now();
            if (!m_refreshing && now >= m_nextRefreshTime)
                requestRefresh();

            // resetsAt trigger
            auto sysNow = system_clock::now();
            bool resetTriggered = false;
            if (m_quota.primary.valid &&
                m_quota.primary.resetsAt.time_since_epoch().count() > 0 &&
                m_quota.primary.resetsAt <= sysNow)
                resetTriggered = true;
            if (m_quota.secondary.valid &&
                m_quota.secondary.resetsAt.time_since_epoch().count() > 0 &&
                m_quota.secondary.resetsAt <= sysNow)
                resetTriggered = true;
            if (resetTriggered && !m_refreshing)
                requestRefresh();
        }
        break;

    case WM_TRAYICON:
        if (LOWORD(lp) == WM_RBUTTONUP)
            showContextMenu();
        break;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDM_REFRESH: requestRefresh(); break;
        case IDM_COPY:    copyStatusToClipboard(); break;
        case IDM_LANG_EN:
            m_language = UiLanguage::English;
            saveSettings();
            updateTooltip(m_quota.tooltipText(m_language));
            break;
        case IDM_LANG_ZH:
            m_language = UiLanguage::Chinese;
            saveSettings();
            updateTooltip(m_quota.tooltipText(m_language));
            break;
        case IDM_EXIT:    DestroyWindow(hwnd);  break;
        }
        break;

    case WM_REFRESH_DONE:
        onRefreshComplete();
        break;

    case WM_DESTROY: {
        m_closing = true;
        KillTimer(hwnd, m_timerId);
        NOTIFYICONDATAW nid = {};
        nid.cbSize = sizeof(nid);
        nid.hWnd   = hwnd;
        nid.uID    = TRAY_UID;
        Shell_NotifyIconW(NIM_DELETE, &nid);
        PostQuitMessage(0);
        break;
    }

    default:
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Tray icon
// ---------------------------------------------------------------------------

void TrayApp::createTrayIcon() {
    m_hIcon = IconRenderer::createLoadingIcon(getTrayIconSize());

    NOTIFYICONDATAW nid = {};
    nid.cbSize           = sizeof(nid);
    nid.hWnd             = m_hwnd;
    nid.uID              = TRAY_UID;
    nid.uFlags           = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon            = m_hIcon;
    wcscpy_s(nid.szTip, m_quota.tooltipText(m_language).c_str());
    Shell_NotifyIconW(NIM_ADD, &nid);

    // Default refresh: 60 s (will be adjusted after first read)
    m_nextRefreshTime = steady_clock::now() + seconds(60);
}

void TrayApp::updateTrayIcon() {
    HICON newIcon = nullptr;
    const int iconSize = getTrayIconSize();

    if (m_refreshing)
        newIcon = IconRenderer::createLoadingIcon(iconSize);
    else if (m_quota.state == QuotaState::CodexNotFound ||
             m_quota.state == QuotaState::CodexNotLoggedIn ||
             m_quota.state == QuotaState::ReadTimeout ||
             m_quota.state == QuotaState::NoQuotaData)
        newIcon = IconRenderer::createErrorIcon(iconSize);
    else if (m_quota.state == QuotaState::ReadFailed) {
        double used5h = m_quota.hasPreviousSuccess ? m_quota.prevPrimary.usedPercent   : -1;
        double used7d = m_quota.hasPreviousSuccess ? m_quota.prevSecondary.usedPercent : -1;
        newIcon = IconRenderer::createIcon(iconSize, used5h, used7d, false, false);
    } else if (m_quota.state == QuotaState::Live) {
        double used5h = m_quota.primary.valid   ? m_quota.primary.usedPercent   : -1;
        double used7d = m_quota.secondary.valid ? m_quota.secondary.usedPercent : -1;
        newIcon = IconRenderer::createIcon(iconSize, used5h, used7d, false, false);
    } else {
        newIcon = IconRenderer::createLoadingIcon(iconSize);
    }

    if (!newIcon) return;

    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd   = m_hwnd;
    nid.uID    = TRAY_UID;
    nid.uFlags = NIF_ICON;
    nid.hIcon  = newIcon;
    Shell_NotifyIconW(NIM_MODIFY, &nid);

    if (m_hIcon) DestroyIcon(m_hIcon);
    m_hIcon = newIcon;
}

void TrayApp::updateTooltip(const std::wstring& tip) {
    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd   = m_hwnd;
    nid.uID    = TRAY_UID;
    nid.uFlags = NIF_TIP;
    wcsncpy_s(nid.szTip, tip.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

// ---------------------------------------------------------------------------
// Context menu
// ---------------------------------------------------------------------------

void TrayApp::showContextMenu() {
    if (m_hMenu) {
        DestroyMenu(m_hMenu);
        m_hMenu = nullptr;
    }
    m_hMenu = CreatePopupMenu();

    AppendMenuW(m_hMenu, MF_STRING, IDM_REFRESH, tr(L"Refresh now", L"立即刷新").c_str());
    AppendMenuW(m_hMenu, MF_STRING, IDM_COPY,    tr(L"Copy status", L"复制状态").c_str());
    AppendMenuW(m_hMenu, MF_SEPARATOR, 0, nullptr);

    HMENU langMenu = CreatePopupMenu();
    AppendMenuW(langMenu,
                MF_STRING | (m_language == UiLanguage::English ? MF_CHECKED : MF_UNCHECKED),
                IDM_LANG_EN,
                L"English");
    AppendMenuW(langMenu,
                MF_STRING | (m_language == UiLanguage::Chinese ? MF_CHECKED : MF_UNCHECKED),
                IDM_LANG_ZH,
                L"中文");
    AppendMenuW(m_hMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(langMenu),
                tr(L"Language", L"语言").c_str());
    AppendMenuW(m_hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m_hMenu, MF_STRING, IDM_EXIT,    tr(L"Exit", L"退出").c_str());

    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(m_hwnd); // required for TrackPopupMenu to work correctly
    TrackPopupMenu(m_hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN,
                   pt.x, pt.y, 0, m_hwnd, nullptr);
    PostMessageW(m_hwnd, WM_NULL, 0, 0); // benign message to dismiss menu
}

// ---------------------------------------------------------------------------
// Refresh logic
// ---------------------------------------------------------------------------

void TrayApp::requestRefresh() {
    if (m_closing) return;
    if (m_refreshing.exchange(true)) return;   // already running
    if (m_refreshThread.joinable())
        m_refreshThread.join();

    m_quota.state = QuotaState::Refreshing;
    updateTrayIcon();
    updateTooltip(m_quota.tooltipText(m_language));

    m_refreshThread = std::thread([this]() { doRefresh(); });
}

void TrayApp::doRefresh() {
    CodexResult cr = CodexClient::readQuota();

    {
        std::lock_guard<std::mutex> lk(m_mtx);
        m_pendingSuccess = cr.success;
        m_pendingJson    = std::move(cr.json);
        m_pendingError   = std::move(cr.error);
    }
    if (m_closing || !IsWindow(m_hwnd) ||
        !PostMessageW(m_hwnd, WM_REFRESH_DONE, 0, 0)) {
        m_refreshing = false;
    }
}

void TrayApp::onRefreshComplete() {
    std::string json, err;
    bool ok = false;
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        ok  = m_pendingSuccess;
        json = std::move(m_pendingJson);
        err  = std::move(m_pendingError);
    }

    if (ok) {
        bool parsed = m_quota.parseFromJson(json);
        if (!parsed) {
            // parseFromJson already set state + lastError
            if (m_quota.hasPreviousSuccess)
                m_quota.stale = true;
        }
    } else {
        if (err == "Codex not found")
            m_quota.state = QuotaState::CodexNotFound;
        else if (err.find("timed out") != std::string::npos)
            m_quota.state = QuotaState::ReadTimeout;
        else
            m_quota.state = QuotaState::ReadFailed;
        m_quota.lastError = err;

        // Fall back to previous success
        if (m_quota.hasPreviousSuccess) {
            m_quota.stale    = true;
            m_quota.primary   = m_quota.prevPrimary;
            m_quota.secondary = m_quota.prevSecondary;
            m_quota.lastRefresh = m_quota.prevRefresh;
        }
    }

    m_refreshing = false;
    setAdaptiveTimer();
    updateTrayIcon();
    updateTooltip(m_quota.tooltipText(m_language));
}

// ---------------------------------------------------------------------------
// Timer
// ---------------------------------------------------------------------------

int TrayApp::getTrayIconSize() const {
    // Detect DPI and pick the best supported icon size for the tray.
    // Windows tray icon size varies by taskbar setting and DPI.
    UINT dpi = 96;
    if (m_hwnd) {
        // GetDpiForWindow requires Win10 1607+; fall back to device caps if unavailable.
        dpi = GetDpiForWindow(m_hwnd);
        if (dpi == 0) dpi = 96;
    }
    int logical = MulDiv(16, dpi, 96); // base tray icon is ~16 at 96 DPI
    // Round up to nearest supported size
    if (logical <= 16) return 16;
    if (logical <= 20) return 20;
    if (logical <= 24) return 24;
    if (logical <= 32) return 32;
    if (logical <= 40) return 40;
    if (logical <= 48) return 48;
    if (logical <= 64) return 64;
    return 256;
}

// ---------------------------------------------------------------------------
// Timer
// ---------------------------------------------------------------------------

void TrayApp::setAdaptiveTimer() {
    int intervalSec = 180; // 3 min default

    if (m_quota.state == QuotaState::CodexNotFound)
        intervalSec = 180;
    else if (m_quota.state == QuotaState::ReadTimeout ||
             m_quota.state == QuotaState::ReadFailed)
        intervalSec = 60;
    else if (m_quota.primary.valid) {
        double p = m_quota.primary.remainingPercent;
        if (p < 10.0)      intervalSec = 60;
        else if (p < 30.0) intervalSec = 120;
    }

    m_nextRefreshTime = steady_clock::now() + seconds(intervalSec);
}

// ---------------------------------------------------------------------------
// Copy status
// ---------------------------------------------------------------------------

void TrayApp::copyStatusToClipboard() {
    std::wstring text = m_quota.copyStatusText(m_language);
    if (text.empty()) return;

    if (!OpenClipboard(m_hwnd)) return;
    EmptyClipboard();

    size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (hMem) {
        auto* dst = static_cast<wchar_t*>(GlobalLock(hMem));
        if (dst) {
            memcpy(dst, text.c_str(), bytes);
            GlobalUnlock(hMem);
        }
        SetClipboardData(CF_UNICODETEXT, hMem);
    }
    CloseClipboard();
}
