#include "info.h"

#include <pspkernel.h>
#include <psppower.h>
#include <psprtc.h>
#include <stdio.h>

#include "overlay.h"

#define ORANGE 0xFFFF9F43
#define RED 0xFFFF4D4D

volatile Info info = { -1, -1, 0, 1, 1, 0, 0 };

static int worker(SceSize args, void *argp)
{
    (void)args; (void)argp;
    for (;;) {
        if (scePowerIsBatteryExist()) {
            info.charging = scePowerIsPowerOnline();
            int p = scePowerGetBatteryLifePercent();
            if (p >= 0 && p <= 100) info.percent = p;
            info.minutes_left = scePowerGetBatteryLifeTime();
        } else {
            info.percent = -1;
        }
        ScePspDateTime t;
        if (sceRtcGetCurrentClockLocalTime(&t) >= 0) {
            info.month = t.month; info.day = t.day; info.hour = t.hour; info.minute = t.minute;
        }
        sceKernelDelayThread(1000000);
    }
    return 0;
}

void info_start(void)
{
    SceUID th = sceKernelCreateThread("boro_info", worker, 0x30, 0x1000, 0, NULL);
    if (th >= 0) sceKernelStartThread(th, 0, NULL);
}

u32 info_level_color(void)
{
    if (info.charging || info.percent >= 20) return INFO_TEXT;
    return info.percent >= 10 ? ORANGE : RED;
}

void info_battery_icon(int x, int y, u32 c)
{
    ov_rect(x, y, 15, 1, c); ov_rect(x, y + 7, 15, 1, c);
    ov_rect(x, y, 1, 8, c); ov_rect(x + 14, y, 1, 8, c);
    ov_rect(x + 15, y + 2, 2, 4, c);
    int fill = info.percent > 0 ? (info.percent * 11 + 99) / 100 : 0;
    ov_rect(x + 2, y + 2, fill, 4, c);
}

void info_peek_card(int x, int y, int w)
{
    const int h = 74;
    ov_rrect(x - 1, y - 1, w + 2, h + 2, 12, INFO_EDGE);
    ov_rrect(x, y, w, h, 11, INFO_CARD);
    char clock[12];
    int hr = info.hour;
    snprintf(clock, sizeof(clock), "%d:%02d %s", hr % 12 ? hr % 12 : 12, info.minute, hr < 12 ? "AM" : "PM");
    ov_text(x + (w - ov_text_width(clock, 3)) / 2, y + 12, clock, INFO_TEXT, 3);
    if (info.percent < 0) return;

    char pct[8], left[32];
    snprintf(pct, sizeof(pct), "%d%%", info.percent);
    if (info.charging) snprintf(left, sizeof(left), "Charging");
    else if (info.minutes_left > 0) snprintf(left, sizeof(left), "About %dh %02dm left", info.minutes_left / 60, info.minutes_left % 60);
    else left[0] = 0;
    int pw = ov_text_width(pct, 1), lw = ov_text_width(left, 1);
    int lx = x + (w - (pw + 24 + lw)) / 2;
    u32 c = info_level_color();
    ov_text(lx, y + 50, pct, c, 1);
    info_battery_icon(lx + pw + 4, y + 50, c);
    ov_text(lx + pw + 24, y + 50, left, INFO_MUTED, 1);
}
