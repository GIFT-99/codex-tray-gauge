#include "icon_renderer.h"
#ifndef PROPID
#define PROPID ULONG
#endif
#include <gdiplus.h>
#include <algorithm>

// ---------------------------------------------------------------------------
// Colour picker  — based on REMAINING percent (not used)
// ---------------------------------------------------------------------------

void IconRenderer::pickColors(double usedPct, uint8_t& r, uint8_t& g, uint8_t& b) {
    if (usedPct < 0) {
        r = 100; g = 100; b = 100;  // no-data grey
    } else {
        double remaining = 100.0 - usedPct;
        if (remaining >= 50.0)    { r = 0;   g = 200; b = 100; }  // green
        else if (remaining >= 20.0) { r = 240; g = 180; b = 0;   }  // amber
        else                    { r = 220; g = 50;  b = 50;  }  // red
    }
}

// ---------------------------------------------------------------------------
// Drawing primitives (gfx = Gdiplus::Graphics*)
// ---------------------------------------------------------------------------

void IconRenderer::drawFullRing(void* gfx, float cx, float cy,
                                float radius, float thickness,
                                uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    auto* gr = static_cast<Gdiplus::Graphics*>(gfx);
    Gdiplus::Pen pen(Gdiplus::Color(a, r, g, b), thickness);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    gr->DrawEllipse(&pen, cx - radius, cy - radius, radius * 2, radius * 2);
}

void IconRenderer::drawRingSegment(void* gfx, float cx, float cy,
                                   float radius, float thickness,
                                   float startDeg, float sweepDeg,
                                   uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (sweepDeg < 0.5f) return;
    auto* gr = static_cast<Gdiplus::Graphics*>(gfx);
    Gdiplus::Pen pen(Gdiplus::Color(a, r, g, b), thickness);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    gr->DrawArc(&pen, cx - radius, cy - radius, radius * 2, radius * 2,
                startDeg, sweepDeg);
}

void IconRenderer::drawCenterDot(void* gfx, float cx, float cy,
                                 uint8_t r, uint8_t g, uint8_t b) {
    auto* gr = static_cast<Gdiplus::Graphics*>(gfx);
    float dotR = 1.5f;
    Gdiplus::SolidBrush br(Gdiplus::Color(r, g, b));
    gr->FillEllipse(&br, cx - dotR, cy - dotR, dotR * 2, dotR * 2);
}

void IconRenderer::drawErrorCross(void* gfx, float cx, float cy,
                                  uint8_t r, uint8_t g, uint8_t b) {
    auto* gr = static_cast<Gdiplus::Graphics*>(gfx);
    Gdiplus::Pen pen(Gdiplus::Color(r, g, b), 2.5f);
    float d = 5.0f;
    gr->DrawLine(&pen, cx - d, cy - d, cx + d, cy + d);
    gr->DrawLine(&pen, cx + d, cy - d, cx - d, cy + d);
}

// ---------------------------------------------------------------------------
// createIcon
// ---------------------------------------------------------------------------

HICON IconRenderer::createIcon(double primaryPct, double secondaryPct,
                               bool isError, bool isLoading) {
    const int SIZE = 32;
    const float CX = SIZE / 2.0f;
    const float CY = SIZE / 2.0f;
    const float OUTER_R = 14.0f;
    const float INNER_R = 8.0f;
    const float THICK   = 4.5f;

    // Create 32-bit ARGB bitmap (top-down via negative height)
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       = SIZE;
    bmi.bmiHeader.biHeight      = -SIZE; // top-down
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HDC screenDC = GetDC(nullptr);
    HBITMAP hBmp = CreateDIBSection(screenDC, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, screenDC);
    if (!hBmp) return nullptr;

    HDC memDC = CreateCompatibleDC(nullptr);
    HBITMAP oldBmp = static_cast<HBITMAP>(SelectObject(memDC, hBmp));

    // GDI+ draw
    Gdiplus::Graphics g(memDC);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);
    g.Clear(Gdiplus::Color(0, 0, 0, 0)); // transparent

    if (isLoading) {
        // Dim grey rings
        drawFullRing(&g, CX, CY, OUTER_R, THICK, 100, 100, 100, 180);
        drawFullRing(&g, CX, CY, INNER_R, THICK, 100, 100, 100, 120);
        drawCenterDot(&g, CX, CY, 140, 140, 140);
    } else if (isError) {
        // Grey rings + red cross
        drawFullRing(&g, CX, CY, OUTER_R, THICK, 100, 100, 100, 200);
        drawFullRing(&g, CX, CY, INNER_R, THICK, 100, 100, 100, 140);
        drawErrorCross(&g, CX, CY, 220, 80, 80);
    } else {
        // ---------- Outer ring (5h) ----------
        //  Grey = used,  Coloured = remaining
        double primaryRemaining = 100.0 - primaryPct;
        uint8_t r1, g1, b1;
        pickColors(primaryPct, r1, g1, b1);   // input is used%, picker uses remaining

        float colorSweep1 = static_cast<float>(std::clamp(primaryRemaining, 0.0, 100.0) * 3.6);
        if (colorSweep1 >= 360.0f) colorSweep1 = 359.99f;
        float greySweep1 = static_cast<float>(std::clamp(primaryPct, 0.0, 100.0) * 3.6);
        if (greySweep1 >= 360.0f) greySweep1 = 359.99f;

        // Grey used segment (starts where colour ends)
        drawRingSegment(&g, CX, CY, OUTER_R, THICK, -90.0f + colorSweep1, greySweep1, 80, 80, 80, 160);
        // Coloured remaining segment
        drawRingSegment(&g, CX, CY, OUTER_R, THICK, -90.0f, colorSweep1, r1, g1, b1, 255);

        // ---------- Inner ring (7d) ----------
        //  Grey = used,  Blue = remaining
        double secondaryRemaining = 100.0 - secondaryPct;
        float colorSweep2 = static_cast<float>(std::clamp(secondaryRemaining, 0.0, 100.0) * 3.6);
        if (colorSweep2 >= 360.0f) colorSweep2 = 359.99f;
        float greySweep2 = static_cast<float>(std::clamp(secondaryPct, 0.0, 100.0) * 3.6);
        if (greySweep2 >= 360.0f) greySweep2 = 359.99f;

        // Grey used segment
        drawRingSegment(&g, CX, CY, INNER_R, THICK, -90.0f + colorSweep2, greySweep2, 80, 80, 80, 160);
        // Blue remaining segment
        drawRingSegment(&g, CX, CY, INNER_R, THICK, -90.0f, colorSweep2, 50, 150, 240, 255);

        // Centre dot — colour follows outer-ring remaining (most critical)
        drawCenterDot(&g, CX, CY, r1, g1, b1);
    }

    g.Flush();

    // HBITMAP → HICON
    ICONINFO ii = {};
    ii.fIcon = TRUE;
    ii.hbmColor = hBmp;

    // Monochrome mask: fill white so hbmColor alpha channel is used
    HDC hdc = GetDC(nullptr);
    ii.hbmMask = CreateBitmap(SIZE, SIZE, 1, 1, nullptr);  // 1 bpp monochrome
    HDC maskDC = CreateCompatibleDC(hdc);
    HBITMAP oldMaskBmp = (HBITMAP)SelectObject(maskDC, ii.hbmMask);
    RECT rc = {0, 0, SIZE, SIZE};
    FillRect(maskDC, &rc, (HBRUSH)GetStockObject(WHITE_BRUSH));
    SelectObject(maskDC, oldMaskBmp);
    DeleteDC(maskDC);
    ReleaseDC(nullptr, hdc);

    HICON hIcon = CreateIconIndirect(&ii);

    // Cleanup
    DeleteObject(ii.hbmMask);
    SelectObject(memDC, oldBmp);
    DeleteDC(memDC);
    DeleteObject(hBmp);
    return hIcon;
}

HICON IconRenderer::createErrorIcon() {
    return createIcon(-1, -1, true, false);
}

HICON IconRenderer::createLoadingIcon() {
    return createIcon(-1, -1, false, true);
}
