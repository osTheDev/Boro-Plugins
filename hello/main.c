// A test plugin for the foundation: it shows a banner when it loads and names
// each NOTE or SCREEN press, so the hooks can be checked on a real PSP before
// the real plugins are built on them.
#include <pspkernel.h>
#include <pspctrl.h>
#include <stdio.h>
#include <string.h>

#include "../common/ini.h"
#include "../common/keys.h"
#include "../common/overlay.h"

PSP_MODULE_INFO("BoroHello", PSP_MODULE_KERNEL, 1, 0);

static char line1[48], line2[48];
static volatile u32 shown_until;

static void show(const char *a, const char *b, u32 ms)
{
    snprintf(line1, sizeof(line1), "%s", a);
    snprintf(line2, sizeof(line2), "%s", b);
    shown_until = sceKernelGetSystemTimeLow() + ms * 1000;
}

static void draw(void)
{
    if ((int)(shown_until - sceKernelGetSystemTimeLow()) <= 0) return;
    int w = ov_text_width(line1, 2);
    int w2 = ov_text_width(line2, 2);
    if (w2 > w) w = w2;
    int x = (OV_W - w - 24) / 2;
    ov_rect(x, 10, w + 24, 52, 0xE0141620);
    ov_rect(x, 10, w + 24, 2, 0xFF6C8CFF);
    ov_text(x + 12, 14, line1, 0xFFF2F2F5, 2);
    ov_text(x + 12, 34, line2, 0xFFB5B8C4, 2);
}

static const char *button_name(u32 b)
{
    static const struct { u32 bit; const char *name; } names[] = {
        { PSP_CTRL_UP, "Up" }, { PSP_CTRL_DOWN, "Down" }, { PSP_CTRL_LEFT, "Left" }, { PSP_CTRL_RIGHT, "Right" },
        { PSP_CTRL_LTRIGGER, "L" }, { PSP_CTRL_RTRIGGER, "R" }, { PSP_CTRL_TRIANGLE, "Triangle" },
        { PSP_CTRL_SQUARE, "Square" }, { PSP_CTRL_CROSS, "Cross" }, { PSP_CTRL_CIRCLE, "Circle" },
    };
    for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); i++) if (b & names[i].bit) return names[i].name;
    return "?";
}

static void on_key(int key, int event, u32 button)
{
    char what[48];
    const char *k = key == KEY_NOTE ? "NOTE" : "SCREEN";
    if (event == EV_TAP) snprintf(what, sizeof(what), "%s tap", k);
    else if (event == EV_HOLD) snprintf(what, sizeof(what), "%s hold 2s", k);
    else snprintf(what, sizeof(what), "%s + %s", k, button_name(button));
    ini_load();
    char note[32];
    ini_str("hello", "text", "boro.ini not found", note, sizeof(note));
    show(what, note, 2000);
}

int module_start(SceSize args, void *argp)
{
    (void)args; (void)argp;
    ini_load();
    overlay_start(draw);
    keys_start(on_key);
    show("Boro plugins loaded", "Press NOTE or SCREEN", 4000);
    return 0;
}

int module_stop(SceSize args, void *argp)
{
    (void)args; (void)argp;
    return 0;
}
