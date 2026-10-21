#include "ini.h"

#include <pspiofilemgr.h>
#include <string.h>

#define INI_PATH "ms0:/SEPLUGINS/boro.ini"

static char text[4096];

void ini_load(void)
{
    text[0] = 0;
    SceUID fd = sceIoOpen(INI_PATH, PSP_O_RDONLY, 0);
    if (fd < 0) fd = sceIoOpen("ef0:/SEPLUGINS/boro.ini", PSP_O_RDONLY, 0); // PSP Go internal storage
    if (fd < 0) return;
    int n = sceIoRead(fd, text, sizeof(text) - 1);
    sceIoClose(fd);
    text[n > 0 ? n : 0] = 0;
}

static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

static int same(const char *a, int n, const char *b)
{
    int i = 0;
    for (; i < n && b[i]; i++) if (lower(a[i]) != lower(b[i])) return 0;
    return i == n && !b[i];
}

static int trimmed(const char **s, const char *end)
{
    while (*s < end && (**s == ' ' || **s == '\t')) (*s)++;
    while (end > *s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r')) end--;
    return end - *s;
}

// The value of section.key, or NULL; *len gets its length.
static const char *find(const char *section, const char *key, int *len)
{
    int in = 0;
    for (const char *line = text; *line;) {
        const char *end = line;
        while (*end && *end != '\n') end++;
        const char *s = line;
        int n = trimmed(&s, end);
        if (n >= 2 && s[0] == '[' && s[n - 1] == ']') {
            in = same(s + 1, n - 2, section);
        } else if (in && n && s[0] != ';' && s[0] != '#') {
            const char *eq = memchr(s, '=', n);
            if (eq) {
                const char *k = s, *v = eq + 1;
                int kn = trimmed(&k, eq), vn = trimmed(&v, s + n);
                if (same(k, kn, key)) { *len = vn; return v; }
            }
        }
        line = *end ? end + 1 : end;
    }
    return NULL;
}

int ini_int(const char *section, const char *key, int fallback)
{
    int len;
    const char *v = find(section, key, &len);
    if (!v || !len) return fallback;
    if (same(v, len, "on") || same(v, len, "yes") || same(v, len, "true")) return 1;
    if (same(v, len, "off") || same(v, len, "no") || same(v, len, "false")) return 0;
    int sign = 1, out = 0, i = 0;
    if (v[0] == '-') { sign = -1; i = 1; }
    if (i == len) return fallback;
    for (; i < len; i++) {
        if (v[i] < '0' || v[i] > '9') return fallback;
        out = out * 10 + v[i] - '0';
    }
    return sign * out;
}

void ini_str(const char *section, const char *key, const char *fallback, char *out, int size)
{
    int len;
    const char *v = find(section, key, &len);
    if (!v) { v = fallback; len = strlen(fallback); }
    if (len > size - 1) len = size - 1;
    memcpy(out, v, len);
    out[len] = 0;
}
