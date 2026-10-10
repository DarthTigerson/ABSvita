#include "ime.h"

#include <psp2/common_dialog.h>
#include <psp2/ime_dialog.h>
#include <string.h>

#include "sys.h"

#define MAXLEN 512

static uint16_t g_title[128], g_initial[MAXLEN + 1], g_buf[MAXLEN + 1];
static bool g_open;

static void utf8_to_utf16(const char *s, uint16_t *out, int max) {
    int n = 0;
    const unsigned char *p = (const unsigned char *)s;
    while (*p && n < max - 1) {
        uint32_t c;
        if (*p < 0x80) c = *p++;
        else if ((*p & 0xE0) == 0xC0 && p[1]) { c = (*p & 0x1F) << 6 | (p[1] & 0x3F); p += 2; }
        else if ((*p & 0xF0) == 0xE0 && p[1] && p[2]) { c = (*p & 0x0F) << 12 | (p[1] & 0x3F) << 6 | (p[2] & 0x3F); p += 3; }
        else { p++; continue; }
        out[n++] = (uint16_t)c;
    }
    out[n] = 0;
}

static void utf16_to_utf8(const uint16_t *s, char *out, size_t max) {
    size_t n = 0;
    for (; *s && n + 4 < max; s++) {
        uint16_t c = *s;
        if (c < 0x80) out[n++] = (char)c;
        else if (c < 0x800) { out[n++] = (char)(0xC0 | c >> 6); out[n++] = (char)(0x80 | (c & 0x3F)); }
        else { out[n++] = (char)(0xE0 | c >> 12); out[n++] = (char)(0x80 | ((c >> 6) & 0x3F)); out[n++] = (char)(0x80 | (c & 0x3F)); }
    }
    out[n] = 0;
}

bool ime_open(const char *title, const char *initial, int max_len, ImeKind kind) {
    if (g_open) return false;
    if (max_len > MAXLEN) max_len = MAXLEN;
    utf8_to_utf16(title, g_title, 128);
    utf8_to_utf16(initial ? initial : "", g_initial, MAXLEN + 1);
    memset(g_buf, 0, sizeof g_buf);
    SceImeDialogParam p;
    sceImeDialogParamInit(&p);
    p.supportedLanguages = 0;
    p.languagesForced = SCE_FALSE;
    p.type = kind == IME_URL ? SCE_IME_TYPE_URL : SCE_IME_TYPE_DEFAULT;
    p.option = 0;
    p.textBoxMode = kind == IME_PASSWORD ? SCE_IME_DIALOG_TEXTBOX_MODE_PASSWORD : SCE_IME_DIALOG_TEXTBOX_MODE_DEFAULT;
    p.title = g_title;
    p.maxTextLength = max_len;
    p.initialText = g_initial;
    p.inputTextBuffer = g_buf;
    g_open = sceImeDialogInit(&p) >= 0;
    return g_open;
}

bool ime_active(void) { return g_open; }

int ime_poll(char *out, size_t n) {
    if (!g_open) return -1;
    if (sceImeDialogGetStatus() != SCE_COMMON_DIALOG_STATUS_FINISHED) return 0;
    SceImeDialogResult r;
    memset(&r, 0, sizeof r);
    sceImeDialogGetResult(&r);
    int rc = r.button == SCE_IME_DIALOG_BUTTON_ENTER ? 1 : -1;
    if (rc == 1) utf16_to_utf8(g_buf, out, n);
    sceImeDialogTerm();
    g_open = false;
    return rc;
}
