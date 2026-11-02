// Better Bright: 10 backlight steps instead of the firmware's 4, two of them
// dimmer than the firmware goes, and full brightness on battery if asked.
// SCREEN steps through them; SCREEN + Up / Down moves one step; holding
// SCREEN 2 seconds turns the screen off until the next press.
#include <pspctrl.h>
#include <pspdisplay_kernel.h>
#include <pspiofilemgr.h>
#include <pspkernel.h>
#include <psppower.h>
#include <pspsdk.h>
#include <stdio.h>
#include <systemctrl.h>

#include "../common/ini.h"
#include "../common/keys.h"
#include "../common/overlay.h"

PSP_MODULE_INFO("BoroBright", PSP_MODULE_KERNEL, 1, 0);

#define STEPS 10
#define CAP_STEP 8 // steps past this are brighter than the firmware allows on battery
#define NID_SET_BRIGHTNESS 0x9E3C6DC6
#define STATE_PATH "ms0:/SEPLUGINS/boro_bright.txt"
#define BAR_US 1000000
#define IDLE_US 5000000 // a dimming request after this long without input is the firmware's auto-dim

// Backlight level (0-100) for each step. 1 and 2 are the extra-dim steps.
static const int levels[STEPS] = { 30, 36, 44, 52, 58, 64, 72, 84, 92, 100 };

static int step = 7;                       // 1-based
static int full_on_battery, show_bar, extra_dim;
static volatile int dimmed;                // the firmware has dimmed or turned the screen off
static volatile u64 bar_until, save_at;
static volatile int off;               // SCREEN held 2 s turned the screen off
static volatile u64 off_at, woke_at;

static SctrlFunctionPatchData patch;
static void (*set_brightness)(int, int);

static int lowest_step(void) { return extra_dim ? 1 : 3; }

static int highest_step(void)
{
    return full_on_battery || scePowerIsPowerOnline() ? STEPS : CAP_STEP;
}

static void apply(void)
{
    if (step > highest_step()) step = highest_step();
    if (step < lowest_step()) step = lowest_step();
    if (!dimmed) set_brightness(levels[step - 1], 0);
}

// Every brightness change goes through here, the firmware's included. Its own
// SCREEN steps and its 1 second hold-SCREEN screen-off are swallowed (this
// plugin turns the screen off after 2 seconds instead); its auto-dim and
// idle screen-off pass; when it brings the screen back, it comes back at our
// step.
static void hooked_set_brightness(int level, int unk)
{
    int cur = 0, u = 0;
    sceDisplayGetBrightness(&cur, &u);
    u64 now = now_us();
    int screen_key = keys_held(KEY_SCREEN) || now - keys_released_at(KEY_SCREEN) < 500000;
    if (screen_key) return;
    if (level == 0 || (level < cur && now - keys_last_input() >= IDLE_US)) {
        dimmed = 1;
        set_brightness(level, unk);
        return;
    }
    dimmed = 0;
    set_brightness(levels[step - 1], unk);
}

static void change(int to)
{
    step = to;
    apply();
    bar_until = now_us() + BAR_US;
    save_at = now_us() + 2000000;
}

static void on_key(int key, int event, u32 button)
{
    if (key != KEY_SCREEN) return;
    if (event == EV_HOLD) {
        off = dimmed = 1;
        off_at = now_us();
        set_brightness(0, 0);
        return;
    }
    if (event == EV_TAP && now_us() - woke_at < 1000000) return; // the press that woke it
    if (event == EV_TAP) change(step >= highest_step() ? lowest_step() : step + 1);
    else if (event == EV_COMBO && button & PSP_CTRL_UP) change(step < highest_step() ? step + 1 : step);
    else if (event == EV_COMBO && button & PSP_CTRL_DOWN) change(step > lowest_step() ? step - 1 : step);
}

// The bar from the render: dark glass, a sun, ten segments, the step number,
// and a mark where the battery cap is.
static void draw(void)
{
    if (!show_bar || bar_until <= now_us()) return;
    const int x = 130, y = 226, w = 220, h = 32;
    ov_rrect(x, y, w, h, 12, 0xE8161820);
    ov_ring(148, 242, 6, 0xFFF2F2F5);
    ov_rect(147, 232, 2, 3, 0xFFF2F2F5); ov_rect(147, 250, 2, 3, 0xFFF2F2F5);
    ov_rect(138, 241, 3, 2, 0xFFF2F2F5); ov_rect(156, 241, 3, 2, 0xFFF2F2F5);
    for (int i = 0; i < STEPS; i++) {
        u32 c = i < step ? 0xFF6C8CFF : 0xFF464A5A;
        if (i >= CAP_STEP && i < step && !scePowerIsPowerOnline()) c = 0xFFFF9F43; // past the battery cap
        ov_rrect(166 + i * 16, 236, 12, 12, 2, c);
    }
    ov_rect(166 + CAP_STEP * 16 - 3, 232, 1, 20, 0xFFFF9F43);
    char n[4];
    snprintf(n, sizeof(n), "%d", step);
    ov_text(330, 234, n, 0xFFF2F2F5, 2);
}

static void load_step(void)
{
    char buf[8] = { 0 };
    SceUID fd = sceIoOpen(STATE_PATH, PSP_O_RDONLY, 0);
    if (fd < 0) return;
    sceIoRead(fd, buf, sizeof(buf) - 1);
    sceIoClose(fd);
    int v = 0;
    for (char *p = buf; *p >= '0' && *p <= '9'; p++) v = v * 10 + *p - '0';
    if (v >= 1 && v <= STEPS) step = v;
}

static void save_step(void)
{
    SceUID fd = sceIoOpen(STATE_PATH, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (fd < 0) return;
    char buf[8];
    int n = snprintf(buf, sizeof(buf), "%d\n", step);
    sceIoWrite(fd, buf, n);
    sceIoClose(fd);
}

// Saves a changed step once it settles, and lowers it if the charger is
// unplugged above the cap.
static int worker(SceSize args, void *argp)
{
    (void)args; (void)argp;
    for (;;) {
        // Once SCREEN is up after turning the screen off, any press brings it back.
        u64 up = keys_released_at(KEY_SCREEN);
        if (off && up > off_at && keys_last_input() > up) {
            off = dimmed = 0;
            woke_at = now_us();
            apply();
        }
        if (save_at && save_at <= now_us()) { save_at = 0; save_step(); }
        if (step > highest_step()) apply();
        sceKernelDelayThread(off ? 30000 : 250000);
    }
    return 0;
}

int module_start(SceSize args, void *argp)
{
    (void)args; (void)argp;
    ini_load();
    full_on_battery = ini_int("bright", "full_on_battery", 0);
    show_bar = ini_int("bright", "bar", 1);
    extra_dim = ini_int("bright", "extra_dim", 1);
    load_step();

    u32 addr = sctrlHENFindFunction("sceDisplay_Service", "sceDisplay_driver", NID_SET_BRIGHTNESS);
    if (!addr) return 1; // not this firmware: stay out of the way
    sctrlHENHijackFunction(&patch, (void *)addr, hooked_set_brightness, (void **)&set_brightness);
    sctrlFlushCache();
    apply();

    overlay_start(draw);
    keys_start(on_key);
    SceUID th = sceKernelCreateThread("boro_bright", worker, 0x30, 0x1000, 0, NULL);
    if (th >= 0) sceKernelStartThread(th, 0, NULL);
    return 0;
}

int module_stop(SceSize args, void *argp)
{
    (void)args; (void)argp;
    return 0;
}
