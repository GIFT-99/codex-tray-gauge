#define WIN32_LEAN_AND_MEAN
#include <windows.h>
// MinGW GDI+ header workaround: PROPID is not always defined
#ifndef PROPID
#define PROPID ULONG
#endif
#include <gdiplus.h>
#ifdef _MSC_VER
#pragma comment(lib, "gdiplus.lib")
#endif
#include "tray_app.h"

int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int)
{
    // Single-instance mutex
    HANDLE hMutex = CreateMutexW(nullptr, FALSE, L"Local\\CodexTrayGauge_SI");
    if (!hMutex) return 1;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(hMutex);
        return 0;
    }

    // Start GDI+
    Gdiplus::GdiplusStartupInput gdiSI;
    ULONG_PTR gdiToken = 0;
    Gdiplus::GdiplusStartup(&gdiToken, &gdiSI, nullptr);

    int ret = 0;
    {
        TrayApp app(hInstance);
        ret = app.run();
    }

    Gdiplus::GdiplusShutdown(gdiToken);
    CloseHandle(hMutex);
    return ret;
}
