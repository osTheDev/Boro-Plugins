// Drawing over whatever is on screen, in the XMB or a game. The plugin's
// draw function runs each time a frame is shown and paints straight into it.
#ifndef BORO_OVERLAY_H
#define BORO_OVERLAY_H

#include <psptypes.h>

#define OV_W 480
#define OV_H 272

typedef void (*OverlayDraw)(void);

// Hooks the display; draw is called once per shown frame. Returns 0 on success.
int overlay_start(OverlayDraw draw);

// Only valid inside the draw function. Colors are 0xAARRGGBB.
void ov_rect(int x, int y, int w, int h, u32 argb);
void ov_text(int x, int y, const char *s, u32 argb, int scale); // y is the top
int ov_text_width(const char *s, int scale);
void ov_rrect(int x, int y, int w, int h, int r, u32 argb);     // filled, rounded corners
void ov_ring(int cx, int cy, int r, u32 argb);                  // 1px circle outline
void ov_read_row(int y, u8 *rgb); // one row of the frame as 8-bit RGB, before this plugin draws

#endif
