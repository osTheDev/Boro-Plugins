// Battery and clock, read once a second by a worker thread (the power and
// clock calls can wait on the system controller, which the display hook must
// never do), plus the pieces every plugin draws them with.
#ifndef BORO_INFO_H
#define BORO_INFO_H

#include <psptypes.h>

#define INFO_TEXT 0xFFF2F2F5
#define INFO_MUTED 0xFFB5B8C4
#define INFO_CARD 0xEE161820
#define INFO_EDGE 0xFF3A4370

typedef struct {
    int percent;      // -1 until read, or with no battery
    int minutes_left; // the firmware's estimate, <= 0 when unknown
    int charging;
    int month, day, hour, minute;
} Info;

extern volatile Info info;

void info_start(void);
u32 info_level_color(void);                 // white, orange under 20%, red under 10%
void info_battery_icon(int x, int y, u32 c); // 17 x 8
// The peek: a card with the clock in large type and the battery under it.
void info_peek_card(int x, int y, int w);

#endif
