# X11 headers

The eight header files in `include/X11/` are everything the X11 backend
(`src/platform_x11.c`) and the test driver include. They are bundled so the
Linux build needs no development package: the program is linked straight
against `libX11.so.6`, which every X11 or XWayland desktop already has.

| Files | From | Version |
|---|---|---|
| `Xlib.h`, `Xutil.h` | libX11 | 1.8.12 |
| `X.h`, `Xatom.h`, `Xfuncproto.h`, `Xosdefs.h`, `keysym.h`, `keysymdef.h` | xorgproto | 2024.1 |

They were taken unmodified from the Debian packages `libx11-dev` and
`x11proto-dev`. Both projects are under MIT-style licences; the notices are in
`COPYING.libX11` and `COPYING.xorgproto` and at the top of each header.

These are interface definitions of a very stable protocol, so they rarely
need updating. To build against the system's own headers instead, install
`libx11-dev` and run `make X11_CFLAGS= X11_LIBS=-lX11`.
