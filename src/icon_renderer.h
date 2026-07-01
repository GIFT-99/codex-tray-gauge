#pragma once
#include <windows.h>
#include <cstdint>

/// GDI+ helpers to build the single-ring tray icon at runtime.
class IconRenderer {
public:
    /// Create a HICON at the given size (default 32×32) suitable for NOTIFYICONDATA.
    /// primaryPct is 0-100 (used percent); secondaryPct is kept for API stability.
    static HICON createIcon(int size, double primaryPct, double secondaryPct,
                            bool isError, bool isLoading);

    /// Convenience: size-aware error / loading icon.
    static HICON createErrorIcon(int size = 32);
    static HICON createLoadingIcon(int size = 32);

private:
    static void drawFullRing(void* gfx, float cx, float cy,
                             float radius, float thickness,
                             uint8_t r, uint8_t g, uint8_t b, uint8_t a);
    static void drawRingSegment(void* gfx, float cx, float cy,
                                float radius, float thickness,
                                float startDeg, float sweepDeg,
                                uint8_t r, uint8_t g, uint8_t b, uint8_t a);
    static void drawErrorCross(void* gfx, float cx, float cy,
                               uint8_t r, uint8_t g, uint8_t b);
    static void pickColors(double usedPct, uint8_t& r, uint8_t& g, uint8_t& b);
};
