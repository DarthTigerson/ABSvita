/* Vita system services: network, clocks, power, battery, Wi-Fi, system settings. */
#ifndef ABSVITA_SYS_H
#define ABSVITA_SYS_H
#include <stdbool.h>

int sys_init(void);          /* call first */
void sys_exit(void);
/* once per frame: keeps the console awake while audio plays */
void sys_tick(bool playing);
bool sys_enter_is_cross(void);   /* system setting: confirm with X (else O) */
int sys_battery(void);           /* percent */
bool sys_charging(void);
bool sys_wifi_connected(void);
void sys_screen(bool on);        /* turn the display off/on (audio continues) */
bool sys_screen_on(void);
const char *sys_model(void);
int sys_language(void);

#endif
