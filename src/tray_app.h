#pragma once
#include <windows.h>
#include <string>
#include <atomic>
#include <mutex>
#include <chrono>
#include "quota_model.h"

// Portable _TRUNCATE (MSVC defines it; MinGW may not)
#ifndef _TRUNCATE
#define _TRUNCATE ((size_t)-1)
#endif

class TrayApp {
public:
    explicit TrayApp(HINSTANCE hInstance);
    ~TrayApp();
    int  run();

private:
    static LRESULT CALLBACK s_WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    void createTrayIcon();
    void updateTrayIcon();
    void updateTooltip(const std::wstring& tip);
    void showContextMenu();

    void requestRefresh();       // called from timer or menu
    void doRefresh();            // runs inside worker thread
    void onRefreshComplete();    // posted back to main thread

    void copyStatusToClipboard();
    void setAdaptiveTimer();

    HINSTANCE   m_hInst;
    HWND        m_hwnd = nullptr;
    HICON       m_hIcon = nullptr;  // current tray icon (owned)
    HMENU       m_hMenu = nullptr;
    UINT_PTR    m_timerId = 0;

    QuotaModel  m_quota;

    // Refresh thread guard
    std::atomic<bool> m_refreshing{false};

    // Result transfer (worker → main)
    std::mutex  m_mtx;
    bool        m_pendingSuccess = false;
    std::string m_pendingJson;
    std::string m_pendingError;

    // Adaptive timer
    std::chrono::steady_clock::time_point m_nextRefreshTime;

    static constexpr const wchar_t* CLASS_NAME = L"CodexTrayGaugeWnd";
    static constexpr UINT   WM_TRAYICON       = WM_APP + 1;
    static constexpr UINT   WM_REFRESH_DONE   = WM_APP + 2;
    static constexpr UINT_PTR TIMER_MAIN      = 1;
    static constexpr UINT   TRAY_UID          = 100;
    static constexpr DWORD  REFRESH_TIMEOUT_MS = 90000;
};
