// Boro Status: battery % on the XMB, an in-game battery corner (SCREEN + L
// turns it on or off) and a 3 second peek at the time and battery
// (SCREEN + Triangle), in the XMB or a game.
#include <pspctrl.h>
#include <pspinit.h>
#include <pspiofilemgr.h>
#include <pspkernel.h>
#include <psppower.h>
#include <psprtc.h>
#include <stdio.h>

#include "../common/ini.h"
#include "../common/keys.h"
#include "../common/overlay.h"

PSP_MODULE_INFO("BoroStatus", PSP_MODULE_KERNEL, 1, 0);

#define STATE_PATH "ms0:/SEPLUGINS/boro_status.txt"
#define PEEK_US 3000000

#define TEXT 0xFFF2F2F5
#define MUTED 0xFFB5B8C4
#define CARD 0xEE161820
#define EDGE 0xFF3A4370
#define ORANGE 0xFFFF9F43
#define RED 0xFFFF4D4D

static int in_vsh, show_xmb, corner;
static volatile u32 peek_until, save_at;

// Read by a worker thread: the power and clock calls can wait on the system
// controller, which the display hook must never do.
static volatile int percent = -1, minutes_left = -1, charging, hour, minute;

static u32 level_color(void)
{
    if (charging || percent >= 20) return TEXT;
    return percent >= 10 ? ORANGE : RED;
}

// A small battery outline filled to the charge.
static void battery_icon(int x, int y, u32 c)
{
    ov_rect(x, y, 15, 1, c); ov_rect(x, y + 7, 15, 1, c);
    ov_rect(x, y, 1, 8, c); ov_rect(x + 14, y, 1, 8, c);
    ov_rect(x + 15, y + 2, 2, 4, c);
    int fill = percent > 0 ? (percent * 11 + 99) / 100 : 0;
    ov_rect(x + 2, y + 2, fill, 4, c);
}

// "87%" and the icon in a dark pill whose edge takes the warning color.
static void battery_pill(int right, int y)
{
    char s[8];
    snprintf(s, sizeof(s), "%d%%", percent);
    u32 c = level_color();
    int w = ov_text_width(s, 1) + 35, x = right - w;
    ov_rrect(x - 1, y - 1, w + 2, 18, 8, c == TEXT ? EDGE : c);
    ov_rrect(x, y, w, 16, 7, CARD);
    ov_text(x + 7, y + 3, s, c, 1);
    battery_icon(right - 24, y + 4, c);
}

static void draw_peek(void)
{
    const int x = 120, y = 99, w = 240, h = 74;
    ov_rrect(x - 1, y - 1, w + 2, h + 2, 12, EDGE);
    ov_rrect(x, y, w, h, 11, CARD);
    char clock[12];
    snprintf(clock, sizeof(clock), "%d:%02d %s", hour % 12 ? hour % 12 : 12, minute, hour < 12 ? "AM" : "PM");
    ov_text(x + (w - ov_text_width(clock, 3)) / 2, y + 12, clock, TEXT, 3);

    char pct[8], left[32];
    snprintf(pct, sizeof(pct), "%d%%", percent);
    if (charging) snprintf(left, sizeof(left), "Charging");
    else if (minutes_left > 0) snprintf(left, sizeof(left), "About %dh %02dm left", minutes_left / 60, minutes_left % 60);
    else left[0] = 0;
    int pw = ov_text_width(pct, 1), lw = ov_text_width(left, 1);
    int lx = x + (w - (pw + 24 + lw)) / 2;
    ov_text(lx, y + 50, pct, level_color(), 1);
    battery_icon(lx + pw + 4, y + 50, level_color());
    ov_text(lx + pw + 24, y + 50, left, MUTED, 1);
}

static void draw(void)
{
    if (percent < 0) return; // no reading yet, or no battery
    if (in_vsh ? show_xmb : corner) battery_pill(472, 6);
    if ((int)(peek_until - sceKernelGetSystemTimeLow()) > 0) draw_peek();
}

static void on_key(int key, int event, u32 button)
{
    if (key != KEY_SCREEN || event != EV_COMBO) return;
    if (button & PSP_CTRL_TRIANGLE) peek_until = sceKernelGetSystemTimeLow() + PEEK_US;
    if ((button & PSP_CTRL_LTRIGGER) && !in_vsh) {
        corner = !corner;
        save_at = sceKernelGetSystemTimeLow() + 1000000;
    }
}

static void load_corner(void)
{
    char c = 0;
    SceUID fd = sceIoOpen(STATE_PATH, PSP_O_RDONLY, 0);
    if (fd < 0) return;
    sceIoRead(fd, &c, 1);
    sceIoClose(fd);
    corner = c == '1';
}

static void save_corner(void)
{
    SceUID fd = sceIoOpen(STATE_PATH, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (fd < 0) return;
    sceIoWrite(fd, corner ? "1\n" : "0\n", 2);
    sceIoClose(fd);
}

static int worker(SceSize args, void *argp)
{
    (void)args; (void)argp;
    for (;;) {
        if (scePowerIsBatteryExist()) {
            charging = scePowerIsPowerOnline();
            int p = scePowerGetBatteryLifePercent();
            if (p >= 0 && p <= 100) percent = p;
            minutes_left = scePowerGetBatteryLifeTime();
        } else {
            percent = -1;
        }
        ScePspDateTime t;
        if (sceRtcGetCurrentClockLocalTime(&t) >= 0) { hour = t.hour; minute = t.minute; }
        if (save_at && (int)(save_at - sceKernelGetSystemTimeLow()) <= 0) { save_at = 0; save_corner(); }
        sceKernelDelayThread(1000000);
    }
    return 0;
}

int module_start(SceSize args, void *argp)
{
    (void)args; (void)argp;
    ini_load();
    in_vsh = sceKernelInitKeyConfig() == PSP_INIT_KEYCONFIG_VSH;
    show_xmb = ini_int("status", "xmb", 1);
    load_corner();

    SceUID th = sceKernelCreateThread("boro_status", worker, 0x30, 0x1000, 0, NULL);
    if (th >= 0) sceKernelStartThread(th, 0, NULL);
    overlay_start(draw);
    keys_start(on_key);
    return 0;
}

int module_stop(SceSize args, void *argp)
{
    (void)args; (void)argp;
    return 0;
}
