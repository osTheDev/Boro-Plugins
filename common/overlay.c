#include "overlay.h"

#include <pspdisplay.h>
#include <pspkernel.h>
#include <pspsdk.h>
#include <systemctrl.h>

#include "font.h"

#define NID_SET_FRAME_BUF 0x289D82FE

static OverlayDraw draw_fn;
static SctrlFunctionPatchData patch;
static int (*set_frame_buf)(void *, int, int, int);

// The frame being drawn on.
static u8 *fb;
static int fb_stride, fb_format;

static u32 blend(u32 dst_rgb, u32 argb)
{
    u32 a = argb >> 24, ia = 255 - a;
    if (a == 255) return argb & 0xFFFFFF;
    u32 r = (((argb >> 16) & 0xFF) * a + ((dst_rgb >> 16) & 0xFF) * ia) / 255;
    u32 g = (((argb >> 8) & 0xFF) * a + ((dst_rgb >> 8) & 0xFF) * ia) / 255;
    u32 b = ((argb & 0xFF) * a + (dst_rgb & 0xFF) * ia) / 255;
    return r << 16 | g << 8 | b;
}

// Reads and writes one pixel as 0xRRGGBB, whatever the frame's format.
static void plot(int x, int y, u32 argb)
{
    if (x < 0 || y < 0 || x >= OV_W || y >= OV_H) return;
    int i = y * fb_stride + x;
    if (fb_format == PSP_DISPLAY_PIXEL_FORMAT_8888) {
        u32 *p = (u32 *)fb + i, d = *p;
        u32 rgb = (d & 0xFF) << 16 | (d & 0xFF00) | (d >> 16 & 0xFF); // ABGR in memory
        rgb = blend(rgb, argb);
        *p = 0xFF000000 | (rgb & 0xFF) << 16 | (rgb & 0xFF00) | (rgb >> 16 & 0xFF);
        return;
    }
    u16 *p = (u16 *)fb + i, d = *p;
    u32 r, g, b;
    if (fb_format == PSP_DISPLAY_PIXEL_FORMAT_565) { r = (d & 31) << 3; g = (d >> 5 & 63) << 2; b = (d >> 11 & 31) << 3; }
    else if (fb_format == PSP_DISPLAY_PIXEL_FORMAT_5551) { r = (d & 31) << 3; g = (d >> 5 & 31) << 3; b = (d >> 10 & 31) << 3; }
    else { r = (d & 15) << 4; g = (d >> 4 & 15) << 4; b = (d >> 8 & 15) << 4; }
    u32 rgb = blend(r << 16 | g << 8 | b, argb);
    r = rgb >> 16 & 0xFF; g = rgb >> 8 & 0xFF; b = rgb & 0xFF;
    if (fb_format == PSP_DISPLAY_PIXEL_FORMAT_565) *p = r >> 3 | (g >> 2) << 5 | (b >> 3) << 11;
    else if (fb_format == PSP_DISPLAY_PIXEL_FORMAT_5551) *p = r >> 3 | (g >> 3) << 5 | (b >> 3) << 10 | 0x8000;
    else *p = r >> 4 | (g >> 4) << 4 | (b >> 4) << 8 | 0xF000;
}

void ov_read_row(int y, u8 *rgb)
{
    for (int x = 0; x < OV_W; x++, rgb += 3) {
        int i = y * fb_stride + x;
        u32 r, g, b;
        if (fb_format == PSP_DISPLAY_PIXEL_FORMAT_8888) {
            u32 d = ((u32 *)fb)[i];
            r = d & 0xFF; g = d >> 8 & 0xFF; b = d >> 16 & 0xFF;
        } else {
            u16 d = ((u16 *)fb)[i];
            if (fb_format == PSP_DISPLAY_PIXEL_FORMAT_565) { r = (d & 31) << 3; g = (d >> 5 & 63) << 2; b = (d >> 11 & 31) << 3; }
            else if (fb_format == PSP_DISPLAY_PIXEL_FORMAT_5551) { r = (d & 31) << 3; g = (d >> 5 & 31) << 3; b = (d >> 10 & 31) << 3; }
            else { r = (d & 15) << 4; g = (d >> 4 & 15) << 4; b = (d >> 8 & 15) << 4; }
        }
        rgb[0] = r; rgb[1] = g; rgb[2] = b;
    }
}

void ov_rect(int x, int y, int w, int h, u32 argb)
{
    for (int j = y; j < y + h; j++)
        for (int i = x; i < x + w; i++) plot(i, j, argb);
}

void ov_rrect(int x, int y, int w, int h, int r, u32 argb)
{
    for (int j = 0; j < h; j++) {
        // How far in the corner curve starts on this row.
        int dy = j < r ? r - j : j >= h - r ? j - (h - r - 1) : 0, in = 0;
        if (dy) while (in < r && (r - in) * (r - in) + dy * dy > r * r) in++;
        ov_rect(x + in, y + j, w - 2 * in, 1, argb);
    }
}

void ov_ring(int cx, int cy, int r, u32 argb)
{
    // Midpoint circle: each step plots the eight symmetric points.
    int x = r, y = 0, err = 1 - r;
    while (x >= y) {
        int pts[8][2] = { { x, y }, { y, x }, { -y, x }, { -x, y }, { -x, -y }, { -y, -x }, { y, -x }, { x, -y } };
        for (int i = 0; i < 8; i++) plot(cx + pts[i][0], cy + pts[i][1], argb);
        y++;
        if (err < 0) err += 2 * y + 1;
        else { x--; err += 2 * (y - x) + 1; }
    }
}

static int glyph(char c) { return c < 32 || c > 126 ? '?' - 32 : c - 32; }

int ov_text_width(const char *s, int scale)
{
    int w = 0;
    for (; *s; s++) w += font_widths[glyph(*s)] * scale;
    return w;
}

void ov_text(int x, int y, const char *s, u32 argb, int scale)
{
    for (; *s; s++) {
        int g = glyph(*s);
        for (int row = 0; row < 8; row++) {
            u8 bits = font_rows[g * 8 + row];
            for (int col = 0; col < 7; col++)
                if (bits & 0x80 >> col) ov_rect(x + col * scale, y + row * scale, scale, scale, argb);
        }
        x += font_widths[g] * scale;
    }
}

static int hooked_set_frame_buf(void *top, int stride, int format, int sync)
{
    if (top && draw_fn && stride >= OV_W) {
        int k1 = pspSdkSetK1(0);
        // Through the uncached mirror so the pixels land without a cache flush.
        fb = (u8 *)((u32)top | 0x40000000);
        fb_stride = stride;
        fb_format = format;
        draw_fn();
        pspSdkSetK1(k1);
    }
    return set_frame_buf(top, stride, format, sync);
}

int overlay_start(OverlayDraw draw)
{
    u32 addr = sctrlHENFindFunction("sceDisplay_Service", "sceDisplay", NID_SET_FRAME_BUF);
    if (!addr) return -1;
    draw_fn = draw;
    sctrlHENHijackFunction(&patch, (void *)addr, hooked_set_frame_buf, (void **)&set_frame_buf);
    sctrlFlushCache();
    return 0;
}
