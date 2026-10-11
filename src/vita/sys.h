/* Vita system services: network, clocks, power, battery, Wi-Fi, system settings. */
#ifndef ABSVITA_SYS_H
#define ABSVITA_SYS_H
#include <stdbool.h>

int sys_init(void);          /* call first */
void sys_exit(void);
/* once per frame: keeps the console awake while busy (playing, downloading),
 * suspends after a long idle, follows the app moving to and from the background */
void sys_tick(bool busy, bool input);
bool sys_enter_is_cross(void);   /* system setting: confirm with X (else O) */
int sys_battery(void);           /* percent */
bool sys_charging(void);
bool sys_wifi_connected(void);
void sys_screen(bool on);        /* turn the display off/on (audio continues) */
bool sys_screen_on(void);
bool sys_quit_requested(void);   /* the system asked the app to close */
bool sys_visible(void);          /* display on and the app in front: worth drawing */
const char *sys_model(void);
int sys_language(void);

#endif
