// Boro Status: battery % on the XMB and an in-game battery corner (SCREEN + L
// turns either on or off where you are) and a 3 second peek at the time and battery
// (SCREEN + Triangle), in the XMB or a game.
#include <pspctrl.h>
#include <pspinit.h>
#include <pspiofilemgr.h>
#include <pspkernel.h>
#include <stdio.h>

#include "../common/info.h"
#include "../common/ini.h"
#include "../common/keys.h"
#include "../common/overlay.h"

PSP_MODULE_INFO("BoroStatus", PSP_MODULE_KERNEL, 1, 0);

#define STATE_PATH "ms0:/SEPLUGINS/boro_status.txt"
#define PEEK_US 3000000


static int in_vsh, show_xmb, corner;
static volatile u32 peek_until, save_at;

// "87%" and the icon in a dark pill whose edge takes the warning color.
static void battery_pill(int right, int y)
{
    char s[8];
    snprintf(s, sizeof(s), "%d%%", info.percent);
    u32 c = info_level_color();
    int w = ov_text_width(s, 1) + 35, x = right - w;
    ov_rrect(x - 1, y - 1, w + 2, 18, 8, c == INFO_TEXT ? INFO_EDGE : c);
    ov_rrect(x, y, w, 16, 7, INFO_CARD);
    ov_text(x + 7, y + 3, s, c, 1);
    info_battery_icon(right - 24, y + 4, c);
}

// The XMB's own clock reads like "1/3 3:35" and ends where its battery icon
// starts, so its left edge moves as the digits change. Measured from a photo
// of the XMB: about 10 px per digit and 5 px per separator.
#define XMB_CLOCK_RIGHT 413
#define XMB_TEXT_TOP 6

static int digits(int v) { return v >= 10 ? 2 : 1; }

// Plain "71%" just left of the XMB's date, with a shadow like the XMB's text.
static void xmb_percent(void)
{
    int n = digits(info.month) + digits(info.day) + digits(info.hour) + 2;
    int clock_left = XMB_CLOCK_RIGHT - (n * 10 + 3 * 5);
    char s[8];
    snprintf(s, sizeof(s), "%d%%", info.percent);
    int x = clock_left - 8 - ov_text_width(s, 2), y = XMB_TEXT_TOP - 3 * 2; // the font's glyphs start on row 3
    ov_text(x + 1, y + 1, s, 0x99000000, 2);
    ov_text(x, y, s, info_level_color(), 2);
}

static void draw(void)
{
    if (info.percent < 0) return; // no reading yet, or no battery
    if (in_vsh && show_xmb) xmb_percent();
    else if (!in_vsh && corner) battery_pill(472, 6);
    if ((int)(peek_until - sceKernelGetSystemTimeLow()) > 0) info_peek_card(120, 99, 240);
}

static void on_key(int key, int event, u32 button)
{
    if (key != KEY_SCREEN || event != EV_COMBO) return;
    if (button & PSP_CTRL_TRIANGLE) peek_until = sceKernelGetSystemTimeLow() + PEEK_US;
    if (button & PSP_CTRL_LTRIGGER) {
        if (in_vsh) show_xmb = !show_xmb;
        else corner = !corner;
        save_at = sceKernelGetSystemTimeLow() + 1000000;
    }
}

// Two digits: the in-game corner, then the XMB %, each 1 for shown.
static void load_state(void)
{
    char c[2] = { 0 };
    SceUID fd = sceIoOpen(STATE_PATH, PSP_O_RDONLY, 0);
    if (fd < 0) return;
    int n = sceIoRead(fd, c, 2);
    sceIoClose(fd);
    if (n >= 1) corner = c[0] == '1';
    if (n >= 2) show_xmb = c[1] == '1';
}

static void save_state(void)
{
    SceUID fd = sceIoOpen(STATE_PATH, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (fd < 0) return;
    char c[3] = { corner ? '1' : '0', show_xmb ? '1' : '0', '\n' };
    sceIoWrite(fd, c, 3);
    sceIoClose(fd);
}

// Saves a toggle once it settles.
static int worker(SceSize args, void *argp)
{
    (void)args; (void)argp;
    for (;;) {
        if (save_at && (int)(save_at - sceKernelGetSystemTimeLow()) <= 0) { save_at = 0; save_state(); }
        sceKernelDelayThread(250000);
    }
    return 0;
}

int module_start(SceSize args, void *argp)
{
    (void)args; (void)argp;
    ini_load();
    in_vsh = sceKernelInitKeyConfig() == PSP_INIT_KEYCONFIG_VSH;
    show_xmb = ini_int("status", "xmb", 1);
    load_state();
    info_start();

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
