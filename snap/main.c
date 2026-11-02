// Snap: SCREEN + R saves a screenshot to ms0:/PICTURE/Screenshots/ without
// pausing the game. The shown frame is copied in one go, then written out by
// a low-priority thread while the game carries on. A white edge flash and a
// camera icon confirm it; the icon gets a check once the file is saved.
#include <pspctrl.h>
#include <pspinit.h>
#include <pspiofilemgr.h>
#include <pspkernel.h>
#include <pspsysmem_kernel.h>
#include <psprtc.h>
#include <stdio.h>
#include <string.h>

#include "../common/info.h"
#include "../common/ini.h"
#include "../common/keys.h"
#include "../common/overlay.h"
#include "png.h"

PSP_MODULE_INFO("BoroSnap", PSP_MODULE_KERNEL, 1, 0);

#define DIR "ms0:/PICTURE/Screenshots"
#define FLASH_US 400000
#define ICON_US 1500000
#define SHOT_BYTES (OV_W * OV_H * 3)

enum { IDLE, WANTED, CAPTURED };
static volatile int stage = IDLE, saved, failed;
static volatile u64 flash_at, icon_until;
static SceUID block = -1;
static u8 *shot;
static int show_flash, show_icon, in_vsh;

static void draw_camera(void)
{
    const int x = 404, y = 238, w = 62, h = 24;
    ov_rrect(x - 1, y - 1, w + 2, h + 2, 8, INFO_EDGE);
    ov_rrect(x, y, w, h, 7, INFO_CARD);
    // Camera: body, the bump on top, the lens.
    u32 c = failed ? 0xFFFF4D4D : INFO_TEXT;
    ov_rect(x + 10, y + 7, 20, 1, c); ov_rect(x + 10, y + 19, 20, 1, c);
    ov_rect(x + 10, y + 7, 1, 13, c); ov_rect(x + 29, y + 7, 1, 13, c);
    ov_rect(x + 15, y + 5, 7, 2, c);
    ov_ring(x + 20, y + 13, 4, c);
    if (saved) {
        // Check mark, two strokes.
        for (int i = 0; i < 4; i++) ov_rect(x + 38 + i, y + 12 + i, 2, 2, 0xFF6C8CFF);
        for (int i = 0; i < 9; i++) ov_rect(x + 41 + i, y + 15 - i, 2, 2, 0xFF6C8CFF);
    } else if (failed) {
        ov_text(x + 40, y + 6, "!", c, 2);
    }
}

static void draw_flash(u32 since)
{
    int a = 230 - (int)(since * 230 / FLASH_US);
    for (int i = 0; i < 8 && a > 0; i++, a = a * 3 / 4) {
        u32 c = (u32)a << 24 | 0xFFFFFF;
        ov_rect(i, i, OV_W - 2 * i, 1, c);
        ov_rect(i, OV_H - 1 - i, OV_W - 2 * i, 1, c);
        ov_rect(i, i + 1, 1, OV_H - 2 * i - 2, c);
        ov_rect(OV_W - 1 - i, i + 1, 1, OV_H - 2 * i - 2, c);
    }
}

static void draw(void)
{
    // Copy before drawing anything of ours, so the flash isn't in the shot.
    if (stage == WANTED) {
        for (int y = 0; y < OV_H; y++) ov_read_row(y, shot + y * OV_W * 3);
        stage = CAPTURED;
        flash_at = now_us();
    }
    u64 now = now_us();
    if (show_flash && flash_at && now - flash_at < FLASH_US) draw_flash((u32)(now - flash_at));
    if (show_icon && icon_until > now) draw_camera();
}

static void on_key(int key, int event, u32 button)
{
    if (key != KEY_SCREEN || event != EV_COMBO || !(button & PSP_CTRL_RTRIGGER) || stage != IDLE) return;
    // Kernel memory, so the game's own memory is never touched.
    block = sceKernelAllocPartitionMemory(1, "boro_snap", PSP_SMEM_High, SHOT_BYTES, NULL);
    if (block < 0) {
        failed = 1;
        saved = 0;
        icon_until = now_us() + ICON_US;
        return;
    }
    shot = sceKernelGetBlockHeadAddr(block);
    saved = failed = 0;
    icon_until = now_us() + ICON_US + 2000000;
    stage = WANTED;
}

// The game's ID from the disc (ULUS10041), or XMB / GAME.
static void game_id(char *out, int size)
{
    snprintf(out, size, in_vsh ? "XMB" : "GAME");
    if (in_vsh) return;
    char buf[16] = { 0 };
    SceUID fd = sceIoOpen("disc0:/UMD_DATA.BIN", PSP_O_RDONLY, 0);
    if (fd < 0) return;
    int n = sceIoRead(fd, buf, 10);
    sceIoClose(fd);
    if (n < 10) return;
    int j = 0;
    for (int i = 0; i < 10 && j < size - 1; i++)
        if ((buf[i] >= 'A' && buf[i] <= 'Z') || (buf[i] >= '0' && buf[i] <= '9')) out[j++] = buf[i];
    out[j] = 0;
}

static int saver(SceSize args, void *argp)
{
    (void)args; (void)argp;
    for (;;) {
        if (stage != CAPTURED) {
            sceKernelDelayThread(50000);
            continue;
        }
        char id[16], path[96];
        game_id(id, sizeof(id));
        ScePspDateTime t;
        memset(&t, 0, sizeof(t));
        sceRtcGetCurrentClockLocalTime(&t);
        sceIoMkdir("ms0:/PICTURE", 0777);
        sceIoMkdir(DIR, 0777);
        snprintf(path, sizeof(path), DIR "/%s_%04d-%02d-%02d_%02d%02d%02d.png", id, t.year, t.month, t.day, t.hour, t.minute, t.second);
        int ok = png_write(path, shot, OV_W, OV_H) == 0;
        sceKernelFreePartitionMemory(block);
        block = -1;
        saved = ok;
        failed = !ok;
        icon_until = now_us() + ICON_US;
        stage = IDLE;
    }
    return 0;
}

int module_start(SceSize args, void *argp)
{
    (void)args; (void)argp;
    ini_load();
    show_flash = ini_int("snap", "flash", 1);
    show_icon = ini_int("snap", "icon", 1);
    in_vsh = sceKernelInitKeyConfig() == PSP_INIT_KEYCONFIG_VSH;
    overlay_start(draw);
    keys_start(on_key);
    // Lowest priority, so writing the file never takes time from the game.
    SceUID th = sceKernelCreateThread("boro_snap", saver, 0x6F, 0x2000, 0, NULL);
    if (th >= 0) sceKernelStartThread(th, 0, NULL);
    return 0;
}

int module_stop(SceSize args, void *argp)
{
    (void)args; (void)argp;
    return 0;
}
