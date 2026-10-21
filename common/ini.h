// The shared settings file, ms0:/SEPLUGINS/boro.ini: [section] lines and
// key=value lines. Boro's Settings > Plugins page writes it.
#ifndef BORO_INI_H
#define BORO_INI_H

// Reads the file again; call when a setting may have changed.
void ini_load(void);
int ini_int(const char *section, const char *key, int fallback);
// Copies the value into out (at most size bytes) or the fallback.
void ini_str(const char *section, const char *key, const char *fallback, char *out, int size);

#endif
