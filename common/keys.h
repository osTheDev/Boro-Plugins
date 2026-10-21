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

#endif
