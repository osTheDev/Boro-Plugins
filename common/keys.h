// NOTE and SCREEN, the PSP's system buttons. Games can't read them, so every
// plugin shortcut lives on them. Only kernel modules see them.
#ifndef BORO_KEYS_H
#define BORO_KEYS_H

#include <psptypes.h>

// Microseconds since boot. 64 bits, so it never wraps around (the 32-bit
// clock wraps every 71 minutes, which broke timers after a while).
u64 now_us(void);

enum { KEY_NOTE, KEY_SCREEN };
enum {
    EV_TAP,   // pressed and let go with nothing else
    EV_HOLD,  // held 2 seconds with nothing else
    EV_COMBO, // another button pressed while it's held; button says which
};

typedef void (*KeyHandler)(int key, int event, u32 button);

// Starts a thread that watches the buttons and calls handler from it.
void keys_start(KeyHandler handler);

int keys_held(int key);           // NOTE or SCREEN is down right now
u64 keys_released_at(int key);    // system time (us) it last came up
u64 keys_last_input(void);        // system time (us) any button last changed
int keys_combo(int key);          // its current or last press was part of a combo

#endif
