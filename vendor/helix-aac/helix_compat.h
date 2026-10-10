/* ABS3DS: replaces the Arduino/ESP headers used by the ESP8266Audio port of Helix AAC. */
#ifndef HELIX_COMPAT_H
#define HELIX_COMPAT_H
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define PROGMEM
#define pgm_read_byte(p) (*(const uint8_t *)(p))
#define pgm_read_word(p) (*(const uint16_t *)(p))
#endif
