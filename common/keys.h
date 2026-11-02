// NOTE and SCREEN, the PSP's system buttons. Games can't read them, so every
// plugin shortcut lives on them. Only kernel modules see them.
#ifndef BORO_KEYS_H
#define BORO_KEYS_H

#include <psptypes.h>

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
u32 keys_released_at(int key);    // system time (us) it last came up
u32 keys_last_input(void);        // system time (us) any button last changed
int keys_combo(int key);          // its current or last press was part of a combo

#endif
