/* Windows 8.1 and later have a media overlay: what is playing, with buttons,
 * shown beside the volume when it changes, on the lock screen and in the
 * taskbar's quick settings. Players that announce themselves to it (the
 * "System Media Transport Controls") also get the media keys from it. This
 * is the player's side of it, used by platform_win32.c. */
#ifndef SMTC_H
#define SMTC_H

#include <windows.h>

/* What the overlay may ask for. */
enum { SMTC_PLAY, SMTC_PAUSE, SMTC_STOP, SMTC_NEXT, SMTC_PREVIOUS };

/* Announces the player, as the program that owns `window`. A request then
 * arrives at that window as `message`, with the SMTC_* value in wParam; it
 * is posted, so it comes in with the window's other messages. Returns 0 on
 * a system without the overlay, in which case the other functions do
 * nothing. */
int  smtc_attach(HWND window, UINT message);
/* To be called before that window is destroyed; does nothing for another. */
void smtc_detach(HWND window);
/* What is playing: state 0 stopped, 1 playing, 2 paused; title in UTF-8.
 * Call as often as convenient; the overlay is told only of changes. */
void smtc_update(int state, const char *title);

#endif
