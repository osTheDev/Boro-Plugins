// Hold+: HOLD shows a lock card, then turns the screen off after 2 seconds,
// in the XMB and in games. Nudging HOLD off and back on within a second
// peeks at the clock and battery without unlocking. For 1.5 seconds after
// HOLD comes off, a flick on into sleep is blocked, since pushing the switch
// too far is easy; the next flick sleeps as usual.
#include <pspctrl.h>
#include <pspdisplay_kernel.h>
#include <pspkernel.h>
#include <psppower.h>
#include <stdio.h>

#include "../common/info.h"
#include "../common/ini.h"
#include "../common/keys.h"
#include "../common/overlay.h"

PSP_MODULE_INFO("BoroHold", PSP_MODULE_KERNEL, 1, 0);

int scePowerIsRequest(void);
int scePowerCancelRequest(void);

#define LOCK_US 2000000  // lock card, then screen off
#define NUDGE_US 1000000 // HOLD back on this soon after coming off is a peek
#define PEEK_US 3000000
#define GUARD_US 1500000 // sleep blocked this long after HOLD comes off
#define PILL_US 3000000
#define POLL_US 20000

enum { OPEN, LOCKING, LOCKED, DARK, PEEK };
static volatile int state = OPEN;
static volatile u64 lock_at, peek_until, pill_until;
static int screen_off, saved_level = -1;

static void backlight(int on)
{
    if (!on) {
        int level = 0, unk = 0;
        sceDisplayGetBrightness(&level, &unk);
        if (level > 0) saved_level = level;
        sceDisplaySetBrightness(0, 0);
    } else if (saved_level > 0) {
        sceDisplaySetBrightness(saved_level, 0); // Better Bright, if on, puts back its own step
    }
}

static void draw_lock_card(void)
{
    ov_rect(0, 0, OV_W, OV_H, 0x73000000);
    const int x = 170, y = 81, w = 140, h = 110, cx = OV_W / 2;
    ov_rrect(x - 1, y - 1, w + 2, h + 2, 12, INFO_EDGE);
    ov_rrect(x, y, w, h, 11, INFO_CARD);
    // Padlock: a shackle, then the body over its lower half, then a keyhole.
    ov_ring(cx, y + 22, 7, INFO_TEXT);
    ov_ring(cx, y + 22, 6, INFO_TEXT);
    ov_rrect(cx - 11, y + 22, 22, 17, 3, INFO_TEXT);
    ov_rect(cx - 1, y + 28, 2, 6, INFO_CARD);
    ov_text(cx - ov_text_width("Locked", 2) / 2, y + 42, "Locked", INFO_TEXT, 2);

    u32 elapsed = (u32)(now_us() - lock_at);
    if (screen_off) {
        char line[24];
        int secs = (int)(LOCK_US - elapsed + 999999) / 1000000;
        snprintf(line, sizeof(line), "Screen off in %ds", secs < 1 ? 1 : secs);
        ov_text(cx - ov_text_width(line, 1) / 2, y + 68, line, INFO_MUTED, 1);
    }
    int lit = elapsed * 3 / LOCK_US;
    for (int i = 0; i < 3; i++) ov_rrect(cx - 16 + i * 12, y + 90, 6, 6, 3, i <= lit ? 0xFF6C8CFF : 0xFF464A5A);
}

static void draw_pill(void)
{
    const char *s = "Sleep blocked - flick again to sleep";
    int w = ov_text_width(s, 1) + 28, x = (OV_W - w) / 2, y = 10;
    ov_rrect(x - 1, y - 1, w + 2, 26, 12, INFO_EDGE);
    ov_rrect(x, y, w, 24, 11, INFO_CARD);
    ov_text(x + 14, y + 7, s, INFO_TEXT, 1);
}

static void draw(void)
{
    u64 now = now_us();
    if (state == LOCKING) draw_lock_card();
    if (state == PEEK) {
        ov_rect(0, 0, OV_W, OV_H, 0xFF000000);
        info_peek_card(120, 99, 240);
    }
    if (pill_until > now) draw_pill();
}

static int worker(SceSize args, void *argp)
{
    (void)args; (void)argp;
    SceCtrlData pad;
    sceCtrlPeekBufferPositive(&pad, 1);
    int was_hold = (pad.Buttons & PSP_CTRL_HOLD) != 0, was_dark = 0, guarding = 0;
    u64 unlock_at = 0;
    for (;;) {
        sceCtrlPeekBufferPositive(&pad, 1);
        int hold = (pad.Buttons & PSP_CTRL_HOLD) != 0;
        u64 now = now_us();

        if (hold && !was_hold) {
            if (was_dark && now - unlock_at < NUDGE_US) {
                state = PEEK;
                peek_until = now + PEEK_US;
                backlight(1);
            } else {
                state = LOCKING;
                lock_at = now;
            }
        } else if (!hold && was_hold) {
            was_dark = state == DARK || state == PEEK;
            if (was_dark) backlight(1);
            state = OPEN;
            unlock_at = now;
            if (!guarding) scePowerLock(0);
            guarding = 1;
        }
        was_hold = hold;

        if (state == LOCKING && now - lock_at >= LOCK_US) {
            if (screen_off) backlight(0);
            state = screen_off ? DARK : LOCKED;
        }
        if (state == PEEK && peek_until <= now) {
            backlight(0);
            state = DARK;
        }

        // The power lock holds a sleep request back; dropping it here means
        // only a flick after the guard sleeps.
        if (guarding) {
            if (scePowerIsRequest()) {
                scePowerCancelRequest();
                pill_until = now + PILL_US;
            }
            if (now - unlock_at >= GUARD_US) {
                scePowerUnlock(0);
                guarding = 0;
            }
        }
        sceKernelDelayThread(POLL_US);
    }
    return 0;
}

int module_start(SceSize args, void *argp)
{
    (void)args; (void)argp;
    ini_load();
    screen_off = ini_int("hold", "screen_off", 1);
    info_start();
    overlay_start(draw);
    SceUID th = sceKernelCreateThread("boro_hold", worker, 0x18, 0x1000, 0, NULL);
    if (th >= 0) sceKernelStartThread(th, 0, NULL);
    return 0;
}

int module_stop(SceSize args, void *argp)
{
    (void)args; (void)argp;
    return 0;
}
