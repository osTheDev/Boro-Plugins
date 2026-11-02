#include "png.h"

#include <pspiofilemgr.h>
#include <string.h>

#define STORED_MAX 65535

static u32 crc_table[256];

typedef struct {
    SceUID fd;
    u8 buf[16384];
    int n, failed;
    u32 crc, adler_a, adler_b;
    u32 block_left, raw_left; // bytes left in this stored block, and in the whole image
} Writer;

static void make_crc_table(void)
{
    for (u32 n = 0; n < 256; n++) {
        u32 c = n;
        for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >> 1) : c >> 1;
        crc_table[n] = c;
    }
}

static void flush(Writer *w)
{
    if (w->n && sceIoWrite(w->fd, w->buf, w->n) != w->n) w->failed = 1;
    w->n = 0;
}

// Bytes outside any chunk's CRC: the signature and each chunk's length.
static void put_plain(Writer *w, const u8 *p, int n)
{
    while (n--) {
        if (w->n == sizeof(w->buf)) flush(w);
        w->buf[w->n++] = *p++;
    }
}

static void put(Writer *w, const u8 *p, int n)
{
    for (int i = 0; i < n; i++) w->crc = crc_table[(w->crc ^ p[i]) & 0xFF] ^ (w->crc >> 8);
    put_plain(w, p, n);
}

static void put_be32(Writer *w, u32 v, int in_crc)
{
    u8 b[4] = { v >> 24, v >> 16, v >> 8, v };
    if (in_crc) put(w, b, 4); else put_plain(w, b, 4);
}

static void begin_chunk(Writer *w, const char *type, u32 len)
{
    put_be32(w, len, 0);
    w->crc = 0xFFFFFFFF;
    put(w, (const u8 *)type, 4);
}

static void end_chunk(Writer *w) { put_be32(w, w->crc ^ 0xFFFFFFFF, 0); }

// Image bytes, split into stored deflate blocks as they go.
static void put_raw(Writer *w, const u8 *p, int n)
{
    while (n > 0) {
        if (!w->block_left) {
            u32 len = w->raw_left < STORED_MAX ? w->raw_left : STORED_MAX;
            u8 head[5] = { len == w->raw_left, len, len >> 8, ~len, ~len >> 8 };
            put(w, head, 5);
            w->block_left = len;
        }
        int take = n < (int)w->block_left ? n : (int)w->block_left;
        for (int i = 0; i < take; i++) {
            w->adler_a = (w->adler_a + p[i]) % 65521;
            w->adler_b = (w->adler_b + w->adler_a) % 65521;
        }
        put(w, p, take);
        w->block_left -= take;
        w->raw_left -= take;
        p += take;
        n -= take;
    }
}

int png_write(const char *path, const u8 *rgb, int width, int height)
{
    static Writer w;
    if (!crc_table[1]) make_crc_table();
    memset(&w, 0, sizeof(w));
    w.fd = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (w.fd < 0) return -1;

    static const u8 sig[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
    put_plain(&w, sig, 8);

    begin_chunk(&w, "IHDR", 13);
    put_be32(&w, width, 1);
    put_be32(&w, height, 1);
    static const u8 ihdr[5] = { 8, 2, 0, 0, 0 }; // 8-bit RGB, no interlace
    put(&w, ihdr, 5);
    end_chunk(&w);

    u32 raw = (u32)height * (1 + width * 3);
    u32 blocks = (raw + STORED_MAX - 1) / STORED_MAX;
    begin_chunk(&w, "IDAT", 2 + blocks * 5 + raw + 4);
    static const u8 zhead[2] = { 0x78, 0x01 };
    put(&w, zhead, 2);
    w.adler_a = 1;
    w.raw_left = raw;
    for (int y = 0; y < height; y++) {
        static const u8 filter = 0;
        put_raw(&w, &filter, 1);
        put_raw(&w, rgb + y * width * 3, width * 3);
    }
    put_be32(&w, w.adler_b << 16 | w.adler_a, 1);
    end_chunk(&w);

    begin_chunk(&w, "IEND", 0);
    end_chunk(&w);
    flush(&w);
    sceIoClose(w.fd);
    if (w.failed) sceIoRemove(path);
    return w.failed ? -1 : 0;
}
