/* The system keyboard (IME dialog), non-blocking. */
#ifndef ABSVITA_IME_H
#define ABSVITA_IME_H
#include <stdbool.h>
#include <stddef.h>

typedef enum { IME_TEXT, IME_URL, IME_PASSWORD } ImeKind;

bool ime_open(const char *title, const char *initial, int max_len, ImeKind kind);
bool ime_active(void);
/* While open: 0 running; 1 done (out filled); -1 cancelled. Closes the dialog when finished. */
int ime_poll(char *out, size_t n);

#endif
