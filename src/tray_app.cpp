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
};

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
    g_app = this;
}

TrayApp::~TrayApp() {
    if (m_hIcon) { DestroyIcon(m_hIcon); m_hIcon = nullptr; }
    if (m_hMenu) { DestroyMenu(m_hMenu); m_hMenu = nullptr; }
    g_app = nullptr;
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
                updateTooltip(m_quota.tooltipText());

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
        case IDM_EXIT:    DestroyWindow(hwnd);  break;
        }
        break;

    case WM_REFRESH_DONE:
        onRefreshComplete();
        break;

    case WM_DESTROY: {
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
    m_hIcon = IconRenderer::createLoadingIcon();

    NOTIFYICONDATAW nid = {};
    nid.cbSize           = sizeof(nid);
    nid.hWnd             = m_hwnd;
    nid.uID              = TRAY_UID;
    nid.uFlags           = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon            = m_hIcon;
    wcscpy_s(nid.szTip, L"Codex Quota\nLoading...");
    Shell_NotifyIconW(NIM_ADD, &nid);

    // Default refresh: 60 s (will be adjusted after first read)
    m_nextRefreshTime = steady_clock::now() + seconds(60);
}

void TrayApp::updateTrayIcon() {
    HICON newIcon = nullptr;

    if (m_refreshing)
        newIcon = IconRenderer::createLoadingIcon();
    else if (m_quota.state == QuotaState::CodexNotFound ||
             m_quota.state == QuotaState::CodexNotLoggedIn ||
             m_quota.state == QuotaState::ReadTimeout ||
             m_quota.state == QuotaState::NoQuotaData)
        newIcon = IconRenderer::createErrorIcon();
    else if (m_quota.state == QuotaState::ReadFailed) {
        // outer=5h(primary), inner=7d(secondary) — 显示已用百分比
        double outer5h = m_quota.hasPreviousSuccess ? m_quota.prevPrimary.usedPercent   : -1;
        double inner7d = m_quota.hasPreviousSuccess ? m_quota.prevSecondary.usedPercent : -1;
        newIcon = IconRenderer::createIcon(outer5h, inner7d, false, false);
    } else if (m_quota.state == QuotaState::Live) {
        // outer=5h(primary), inner=7d(secondary) — 显示已用百分比
        double outer5h = m_quota.primary.valid   ? m_quota.primary.usedPercent   : -1;
        double inner7d = m_quota.secondary.valid ? m_quota.secondary.usedPercent : -1;
        newIcon = IconRenderer::createIcon(outer5h, inner7d, false, false);
    } else {
        newIcon = IconRenderer::createLoadingIcon();
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
    if (!m_hMenu)
        m_hMenu = CreatePopupMenu();
    else {
        // Rebuild
        while (GetMenuItemCount(m_hMenu) > 0)
            RemoveMenu(m_hMenu, 0, MF_BYPOSITION);
    }

    AppendMenuW(m_hMenu, MF_STRING, IDM_REFRESH, L"Refresh now");
    AppendMenuW(m_hMenu, MF_STRING, IDM_COPY,    L"Copy status");
    AppendMenuW(m_hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m_hMenu, MF_STRING, IDM_EXIT,    L"Exit");

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
    if (m_refreshing.exchange(true)) return;   // already running

    m_quota.state = QuotaState::Refreshing;
    updateTrayIcon();
    updateTooltip(L"Codex Quota\nRefreshing...");

    std::thread([this]() { doRefresh(); }).detach();
}

void TrayApp::doRefresh() {
    CodexResult cr = CodexClient::readQuota();

    {
        std::lock_guard<std::mutex> lk(m_mtx);
        m_pendingSuccess = cr.success;
        m_pendingJson    = std::move(cr.json);
        m_pendingError   = std::move(cr.error);
    }
    PostMessageW(m_hwnd, WM_REFRESH_DONE, 0, 0);
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
    updateTooltip(m_quota.tooltipText());
}

// ---------------------------------------------------------------------------
// Timer
// ---------------------------------------------------------------------------

void TrayApp::setAdaptiveTimer() {
    int intervalSec = 300; // 5 min default

    if (m_quota.state == QuotaState::CodexNotFound)
        intervalSec = 600; // check again in 10 min
    else if (m_quota.state == QuotaState::ReadTimeout ||
             m_quota.state == QuotaState::ReadFailed)
        intervalSec = 60;
    else if (m_quota.primary.valid) {
        double p = m_quota.primary.remainingPercent;
        if (p < 10.0)      intervalSec = 120;
        else if (p < 30.0) intervalSec = 180;
    }

    m_nextRefreshTime = steady_clock::now() + seconds(intervalSec);
}

// ---------------------------------------------------------------------------
// Copy status
// ---------------------------------------------------------------------------

void TrayApp::copyStatusToClipboard() {
    std::wstring text = m_quota.copyStatusText();
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
