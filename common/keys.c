#include "keys.h"

#include <pspctrl.h>
#include <pspkernel.h>

#define HOLD_US 2000000
#define POLL_US 16000
#define COMBO_BUTTONS (PSP_CTRL_UP | PSP_CTRL_DOWN | PSP_CTRL_LEFT | PSP_CTRL_RIGHT | PSP_CTRL_LTRIGGER | \
                       PSP_CTRL_RTRIGGER | PSP_CTRL_TRIANGLE | PSP_CTRL_SQUARE | PSP_CTRL_CROSS | PSP_CTRL_CIRCLE)

static KeyHandler handler;

typedef struct { u32 bit; int down, used; u32 since; } Key;

static int thread(SceSize args, void *argp)
{
    (void)args; (void)argp;
    Key keys[2] = { { PSP_CTRL_NOTE, 0, 0, 0 }, { PSP_CTRL_SCREEN, 0, 0, 0 } };
    u32 last = 0;
    for (;;) {
        SceCtrlData pad;
        sceCtrlPeekBufferPositive(&pad, 1);
        u32 now = sceKernelGetSystemTimeLow(), b = pad.Buttons, pressed = b & ~last;
        for (int k = 0; k < 2; k++) {
            Key *key = &keys[k];
            if (b & key->bit) {
                if (!key->down) { key->down = 1; key->used = 0; key->since = now; }
                u32 combo = pressed & COMBO_BUTTONS;
                if (combo) { key->used = 1; handler(k, EV_COMBO, combo); }
                if (!key->used && now - key->since >= HOLD_US) { key->used = 1; handler(k, EV_HOLD, 0); }
            } else if (key->down) {
                key->down = 0;
                if (!key->used) handler(k, EV_TAP, 0);
            }
        }
        last = b;
        sceKernelDelayThread(POLL_US);
    }
    return 0;
}

void keys_start(KeyHandler h)
{
    handler = h;
    SceUID th = sceKernelCreateThread("boro_keys", thread, 0x18, 0x1000, 0, NULL);
    if (th >= 0) sceKernelStartThread(th, 0, NULL);
}
