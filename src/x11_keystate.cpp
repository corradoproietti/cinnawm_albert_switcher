// SPDX-FileCopyrightText: 2026 CinnaWM Contributors
// SPDX-License-Identifier: GPL-3.0-only

#include "x11_keystate.h"

#include <X11/Xlib.h>
#include <X11/keysym.h>

namespace cinnawm {

bool isSuperKeyHeld(void *x11Display) noexcept
{
    auto *dpy = static_cast<Display *>(x11Display);
    if (!dpy) {
        return false;
    }

    char keymap[32]{};
    XQueryKeymap(dpy, keymap);

    auto checkSym = [dpy, &keymap](KeySym sym) noexcept -> bool {
        const KeyCode kc = XKeysymToKeycode(dpy, sym);
        if (kc == 0) {
            return false;
        }
        const unsigned char byte = static_cast<unsigned char>(keymap[kc >> 3]);
        return (byte & (1u << (kc & 7u))) != 0;
    };

    return checkSym(XK_Super_L) || checkSym(XK_Super_R);
}

}  // namespace cinnawm
