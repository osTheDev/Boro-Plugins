// A small PNG writer for 8-bit RGB images. It stores the pixels without
// compression (about 400 KB for a 480 x 272 shot), which needs no memory
// beyond the image and keeps encoding cheap enough to run beside a game.
#ifndef BORO_PNG_H
#define BORO_PNG_H

#include <psptypes.h>

int png_write(const char *path, const u8 *rgb, int w, int h); // 0 on success

#endif
