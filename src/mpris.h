/* Linux desktops route the media keys (keyboard, Bluetooth headphones, the
 * panel's media controls) to players over D-Bus, through the "MPRIS"
 * interface. This is the player's side of it, used by platform_x11.c. */
#ifndef MPRIS_H
#define MPRIS_H

/* What a controller may ask for. */
enum { MPRIS_PLAY, MPRIS_PAUSE, MPRIS_PLAY_PAUSE, MPRIS_STOP, MPRIS_NEXT, MPRIS_PREVIOUS, MPRIS_RAISE, MPRIS_QUIT };

/* Connects and announces the player. Returns 0 if there is no session bus
 * or no D-Bus library, in which case the other functions do nothing. */
int  mpris_init(void);
void mpris_shutdown(void);
/* The connection's file descriptor, to wait on; -1 if not connected. */
int  mpris_fd(void);
/* Answers whatever has arrived; `on_command` is called for each request. */
void mpris_poll(void (*on_command)(int command));
/* What is playing: state 0 stopped, 1 playing, 2 paused; times in seconds.
 * Call as often as convenient; controllers are told only of changes. */
void mpris_update(int state, const char *title, double position, double length);

#endif
