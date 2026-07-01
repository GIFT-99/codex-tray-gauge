#include "icon_renderer.h"
#ifndef PROPID
#define PROPID ULONG
#endif
#include <gdiplus.h>
#include <algorithm>

struct IconSpec {
    int size;
    float cx;
    float cy;
    float ringR;
    float ringThick;
};

static IconSpec makeSpec(int size) {
    // Draw each supported tray size natively so Windows does not scale a thin source.
    if (size <= 16) return {16, 8, 8, 5.85f, 3.4f};
    if (size <= 20) return {20, 10, 10, 7.5f, 4.0f};
    if (size <= 24) return {24, 12, 12, 9.25f, 4.5f};
    if (size <= 32) return {32, 16, 16, 12.6f, 5.75f};
    if (size <= 40) return {40, 20, 20, 16.0f, 7.0f};
    if (size <= 48) return {48, 24, 24, 19.25f, 8.25f};
    if (size <= 64) return {64, 32, 32, 26.0f, 10.5f};
    return {256, 128, 128, 105.5f, 39.0f};
}

void IconRenderer::pickColors(double usedPct, uint8_t& r, uint8_t& g, uint8_t& b) {
    if (usedPct < 0) {
        r = 150; g = 150; b = 150;
    } else {
        double remaining = 100.0 - usedPct;
        if (remaining >= 50.0)      { r = 0;   g = 220; b = 80; }
        else if (remaining >= 20.0) { r = 255; g = 200; b = 0;  }
        else                        { r = 255; g = 60;  b = 60; }
    }
}

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

void IconRenderer::drawErrorCross(void* gfx, float cx, float cy,
                                  uint8_t r, uint8_t g, uint8_t b) {
    auto* gr = static_cast<Gdiplus::Graphics*>(gfx);
    Gdiplus::Pen pen(Gdiplus::Color(255, r, g, b), cx * 0.3125f);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    float d = cx * 0.625f;
    gr->DrawLine(&pen, cx - d, cy - d, cx + d, cy + d);
    gr->DrawLine(&pen, cx + d, cy - d, cx - d, cy + d);
}

HICON IconRenderer::createIcon(int size, double primaryPct, double secondaryPct,
                               bool isError, bool isLoading) {
    (void)secondaryPct;

    IconSpec s = makeSpec(size);
    const float cx = s.cx;
    const float cy = s.cy;
    const float ringR = s.ringR;
    const float ringThick = s.ringThick;

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = size;
    bmi.bmiHeader.biHeight = -size;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HDC screenDC = GetDC(nullptr);
    HBITMAP hBmp = CreateDIBSection(screenDC, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, screenDC);
    if (!hBmp) return nullptr;

    const int renderScale = size <= 64 ? 4 : 1;
    const int renderSize = size * renderScale;

    Gdiplus::Bitmap renderBmp(renderSize, renderSize, PixelFormat32bppARGB);
    Gdiplus::Graphics g(&renderBmp);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetCompositingQuality(Gdiplus::CompositingQualityHighQuality);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);
    g.Clear(Gdiplus::Color(0, 0, 0, 0));

    const float drawCx = cx * renderScale;
    const float drawCy = cy * renderScale;
    const float drawRingR = ringR * renderScale;
    const float drawRingThick = ringThick * renderScale;

    if (isLoading) {
        drawFullRing(&g, drawCx, drawCy, drawRingR, drawRingThick, 150, 150, 150, 150);
    } else if (isError) {
        drawFullRing(&g, drawCx, drawCy, drawRingR, drawRingThick, 150, 150, 150, 90);
        drawErrorCross(&g, drawCx, drawCy, 220, 80, 80);
    } else {
        uint8_t r, green, b;
        pickColors(primaryPct, r, green, b);
        drawFullRing(&g, drawCx, drawCy, drawRingR, drawRingThick, 155, 155, 155, 55);
        double remaining = std::clamp(100.0 - primaryPct, 0.0, 100.0);
        if (remaining >= 99.5) {
            drawFullRing(&g, drawCx, drawCy, drawRingR, drawRingThick, r, green, b, 255);
        } else {
            drawRingSegment(&g, drawCx, drawCy, drawRingR, drawRingThick,
                            -90.0f, static_cast<float>(remaining * 3.6),
                            r, green, b, 255);
        }
    }

    g.Flush();

    HDC memDC = CreateCompatibleDC(nullptr);
    HBITMAP oldBmp = static_cast<HBITMAP>(SelectObject(memDC, hBmp));
    Gdiplus::Graphics out(memDC);
    out.SetCompositingQuality(Gdiplus::CompositingQualityHighQuality);
    out.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    out.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    out.Clear(Gdiplus::Color(0, 0, 0, 0));
    out.DrawImage(&renderBmp, 0, 0, size, size);
    out.Flush();

    ICONINFO ii = {};
    ii.fIcon = TRUE;
    ii.hbmColor = hBmp;

    HDC hdc = GetDC(nullptr);
    ii.hbmMask = CreateBitmap(size, size, 1, 1, nullptr);
    HDC maskDC = CreateCompatibleDC(hdc);
    HBITMAP oldMaskBmp = static_cast<HBITMAP>(SelectObject(maskDC, ii.hbmMask));
    RECT rc = {0, 0, size, size};
    FillRect(maskDC, &rc, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
    SelectObject(maskDC, oldMaskBmp);
    DeleteDC(maskDC);
    ReleaseDC(nullptr, hdc);

    HICON hIcon = CreateIconIndirect(&ii);

    DeleteObject(ii.hbmMask);
    SelectObject(memDC, oldBmp);
    DeleteDC(memDC);
    DeleteObject(hBmp);
    return hIcon;
}

HICON IconRenderer::createErrorIcon(int size) {
    return createIcon(size, -1, -1, true, false);
}

HICON IconRenderer::createLoadingIcon(int size) {
    return createIcon(size, -1, -1, false, true);
}
