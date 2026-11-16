// Boro Playtime: times every game you play and saves it for Boro's Games
// page. Games don't tell plugins when they quit and batteries die, so the
// running session is saved every minute to playtime_now.txt; the next game
// (or Boro) moves it into playtime.txt. Time asleep never counts.
#include <pspinit.h>
#include <pspiofilemgr.h>
#include <pspkernel.h>
#include <psprtc.h>
#include <stdio.h>
#include <string.h>
#include <systemctrl_se.h>

#include "../common/keys.h"

PSP_MODULE_INFO("BoroTime", PSP_MODULE_KERNEL, 1, 0);

#define DIR "ms0:/PSP/GAME/Boro/data"
#define LOG DIR "/playtime.txt"
#define NOW DIR "/playtime_now.txt"
#define TICK_US 1000000
#define MAX_STEP_US 3000000 // a bigger jump between ticks is sleep, not play
#define SAVE_EVERY 60       // seconds

static char path[256], title[128], disc_id[16], category[8], started[40];

// Copies a PARAM.SFO string value (TITLE, DISC_ID, CATEGORY).
static void sfo_get(const u8 *sfo, int size, const char *key, char *out, int out_size)
{
    if (size < 20 || memcmp(sfo, "\0PSF", 4)) return;
    u32 keys = *(u32 *)(sfo + 8), data = *(u32 *)(sfo + 12), n = *(u32 *)(sfo + 16);
    for (u32 i = 0; i < n && 20 + i * 16 + 16 <= (u32)size; i++) {
        const u8 *e = sfo + 20 + i * 16;
        u32 k = keys + *(u16 *)e, len = *(u32 *)(e + 4), off = data + *(u32 *)(e + 12);
        if (k >= (u32)size || off + len > (u32)size || strcmp((const char *)sfo + k, key)) continue;
        if ((int)len >= out_size) len = out_size - 1;
        memcpy(out, sfo + off, len);
        out[len] = 0;
        for (char *c = out; *c; c++)
            if (*c == '\t' || *c == '\n' || *c == '\r') *c = ' ';
        return;
    }
}

// Reads PARAM.SFO from disc0 (ISO, CSO, UMD) or from inside an EBOOT.PBP.
static void read_sfo(const char *file, int in_pbp)
{
    static u8 sfo[4096];
    SceUID fd = sceIoOpen(file, PSP_O_RDONLY, 0);
    if (fd < 0) return;
    int size = 0;
    if (in_pbp) {
        u32 head[10];
        if (sceIoRead(fd, head, sizeof(head)) == sizeof(head) && head[0] == 0x50425000) {
            u32 len = head[3] - head[2];
            sceIoLseek(fd, head[2], PSP_SEEK_SET);
            size = sceIoRead(fd, sfo, len < sizeof(sfo) ? len : sizeof(sfo));
        }
    } else {
        size = sceIoRead(fd, sfo, sizeof(sfo));
    }
    sceIoClose(fd);
    if (size <= 0) return;
    sfo_get(sfo, size, "TITLE", title, sizeof(title));
    sfo_get(sfo, size, "DISC_ID", disc_id, sizeof(disc_id));
    sfo_get(sfo, size, "CATEGORY", category, sizeof(category));
}

static void write_file(const char *file, const char *text, int append)
{
    SceUID fd = sceIoOpen(file, PSP_O_WRONLY | PSP_O_CREAT | (append ? PSP_O_APPEND : PSP_O_TRUNC), 0777);
    if (fd < 0) return;
    sceIoWrite(fd, text, strlen(text));
    sceIoClose(fd);
}

// Test builds only: one line per launch saying what the plugin saw.
static void debug(const char *what)
{
    static char line[512];
    const char *boot = sceKernelInitFileName();
    snprintf(line, sizeof(line), "%s\tkey=%x\tapi=%x\tboot=%s\tpath=%s\ttitle=%s\tcat=%s\n", what,
             sceKernelInitKeyConfig(), sceKernelInitApitype(), boot ? boot : "(none)", path, title, category);
    write_file(DIR "/playtime_debug.txt", line, 1);
}

static int identify(void)
{
    const char *boot = sceKernelInitFileName();
    if (!boot) return 0;
    if (!strncmp(boot, "disc0:", 6)) {
        const char *iso = sctrlSEGetUmdFile();
        strncpy(path, iso && iso[0] ? iso : "UMD", sizeof(path) - 1);
        for (int i = 0; i < 10 && !title[0]; i++) { // disc0 mounts as the game boots
            read_sfo("disc0:/PSP_GAME/PARAM.SFO", 0);
            if (!title[0]) sceKernelDelayThread(500000);
        }
    } else {
        strncpy(path, boot, sizeof(path) - 1);
        read_sfo(boot, 1);
    }
    // Boro isn't a game.
    char low[256];
    int i = 0;
    for (; path[i] && i < 255; i++) low[i] = (path[i] >= 'A' && path[i] <= 'Z') ? path[i] + 32 : path[i];
    low[i] = 0;
    if (strstr(low, "/psp/game/boro/")) return 0;
    if (!title[0]) {
        const char *slash = strrchr(path, '/');
        strncpy(title, slash ? slash + 1 : path, sizeof(title) - 1);
    }
    return 1;
}


// Moves a session left by the last game (or a dead battery) into the log.
static void fold_last_session(void)
{
    static char line[512];
    SceUID fd = sceIoOpen(NOW, PSP_O_RDONLY, 0);
    if (fd < 0) return;
    int n = sceIoRead(fd, line, sizeof(line) - 1);
    sceIoClose(fd);
    if (n > 0) {
        line[n] = 0;
        SceIoStat st;
        if (sceIoGetstat(LOG, &st) < 0)
            write_file(LOG, "# started\tseconds\tdisc_id\tcategory\ttitle\tpath\n", 0);
        write_file(LOG, line, 1);
    }
    sceIoRemove(NOW);
}

static void save(u32 seconds)
{
    static char line[512];
    snprintf(line, sizeof(line), "%s\t%lu\t%s\t%s\t%s\t%s\n", started, (unsigned long)seconds,
             disc_id, category, title, path);
    write_file(NOW, line, 0);
}

static int timer(SceSize args, void *argp)
{
    (void)args; (void)argp;
    sceKernelDelayThread(2000000); // let the game get going first
    sceIoMkdir("ms0:/PSP", 0777);
    sceIoMkdir("ms0:/PSP/GAME", 0777);
    sceIoMkdir("ms0:/PSP/GAME/Boro", 0777);
    sceIoMkdir(DIR, 0777);
    debug("start");
    if (!identify()) {
        debug("skipped");
        return sceKernelExitDeleteThread(0);
    }
    debug("timing");
    fold_last_session();

    ScePspDateTime t;
    sceRtcGetCurrentClockLocalTime(&t);
    snprintf(started, sizeof(started), "%04d-%02d-%02d %02d:%02d:%02d", t.year, t.month, t.day,
             t.hour, t.minute, t.second);

    u64 last = now_us();
    u32 secs = 2, part_us = 0, saved_at = 0; // count the 2 s we waited
    for (;;) {
        sceKernelDelayThread(TICK_US);
        u64 now = now_us(), step = now - last;
        last = now;
        if (step <= MAX_STEP_US) {
            part_us += (u32)step;
            while (part_us >= 1000000) { part_us -= 1000000; secs++; }
        }
        if (secs - saved_at >= SAVE_EVERY || (!saved_at && secs >= 10)) { // first save at 10 s
            save(secs);
            saved_at = secs;
        }
    }
    return 0;
}

int module_start(SceSize args, void *argp)
{
    (void)args; (void)argp;
    if (sceKernelInitKeyConfig() == PSP_INIT_KEYCONFIG_VSH) return 0;
    SceUID th = sceKernelCreateThread("boro_time", timer, 0x6F, 0x2000, 0, NULL);
    if (th >= 0) sceKernelStartThread(th, 0, NULL);
    else debug("no thread");
    return 0;
}

int module_stop(SceSize args, void *argp)
{
    (void)args; (void)argp;
    return 0;
}
