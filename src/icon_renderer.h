#pragma once
#include <windows.h>
#include <cstdint>

/// GDI+ helpers to build the dual-ring tray icon at runtime.
class IconRenderer {
public:
    /// Create a 32×32 HICON suitable for NOTIFYICONDATA.
    /// primaryPct / secondaryPct are 0-100; negative = "no data" (grey).
    static HICON createIcon(double primaryPct, double secondaryPct,
                            bool isError, bool isLoading);

    /// Convenience: solid-colour error / loading icon.
    static HICON createErrorIcon();
    static HICON createLoadingIcon();

private:
    static void drawRingSegment(void* gfx, float cx, float cy,
                                float radius, float thickness,
                                float startDeg, float sweepDeg,
                                uint8_t r, uint8_t g, uint8_t b, uint8_t a);
    static void drawFullRing(void* gfx, float cx, float cy,
                             float radius, float thickness,
                             uint8_t r, uint8_t g, uint8_t b, uint8_t a);
    static void drawCenterDot(void* gfx, float cx, float cy,
                              uint8_t r, uint8_t g, uint8_t b);
    static void drawErrorCross(void* gfx, float cx, float cy,
                               uint8_t r, uint8_t g, uint8_t b);
    static void pickColors(double pct, uint8_t& r, uint8_t& g, uint8_t& b);
};
