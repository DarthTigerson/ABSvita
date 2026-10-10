/* App shell: modes (login / main / player), the sidebar with its mini player,
 * the content view stack, the button-hint bar, toasts and touch hit-testing. */
#ifndef ABSVITA_APP_H
#define ABSVITA_APP_H
#include <stdbool.h>

#include "config.h"
#include "gfx.h"
#include "input.h"
#include "models.h"

/* content area (right of the sidebar, above the hint bar) */
#define SIDEBAR_W 220
#define CX 244
#define CW (SCREEN_W - CX - 20)
#define HINT_H 40
#define CONTENT_BOTTOM (SCREEN_H - HINT_H)

typedef enum { SEC_HOME, SEC_LIBRARY, SEC_SERIES, SEC_AUTHORS, SEC_SEARCH, SEC_DOWNLOADS, SEC_SETTINGS, SEC_COUNT } Section;

typedef struct View View;
struct View {
    void (*update)(View *v, const Input *in, float dt);
    void (*draw)(View *v, bool focused);
    void (*destroy)(View *v);
    /* called when the view becomes the top of the stack again */
    void (*resume)(View *v);
    void *data;
};

extern Config g_cfg;
void app_save_config(void);
const char *app_data_dir(void);

void app_init(void);
void app_update(const Input *in, float dt);
void app_draw(void);

/* navigation */
void app_push(View *v);
void app_pop(void);
void app_focus_sidebar(void);
void app_set_section(Section s, bool focus_content);
void app_open_book(int lib_index);
void app_open_book_summary(const BookSummary *b); /* e.g. a download while offline */
void app_play(const BookSummary *b, double start); /* start < 0: resume */
void app_open_player(void);
void app_close_player(void);
void app_logged_out(void);
void app_logged_in(void);

/* the confirm / back button glyphs (follow the system's X/O setting) */
Glyph g_ok(void);
Glyph g_back(void);
/* clip drawing to the content area below y (scrolling lists) */
void clip_content(float top);
void clip_off(void);

/* feedback */
void app_toast(const char *msg);
/* adds a button hint to this frame's hint bar */
void hint(Glyph g, const char *label);

/* touch: rectangles registered while drawing; a tap calls fn(a, b) */
typedef void (*HitFn)(int a, int b);
void hit_add(float x, float y, float w, float h, HitFn fn, int a, int b);
/* position of the tap being handled */
float app_tap_x(void);
float app_tap_y(void);

/* smooth scrolling */
typedef struct { float y, target; } Scroll;
void scroll_step(Scroll *s, float dt);
/* moves target so [top, bottom) (content coordinates) is visible in a view of height view_h */
void scroll_show(Scroll *s, float top, float bottom, float view_h, float max);
void scroll_drag(Scroll *s, float dy, float max);

/* shared widgets */
void draw_progress_bar(float x, float y, float w, float h, float frac, uint32_t fill, uint32_t track);
void draw_focus_ring(float x, float y, float w, float h, float r);
void draw_spinner(float cx, float cy, float r, uint32_t c);
/* book cell: cover with progress, title and author below */
void draw_book_cell(const BookSummary *b, float x, float y, float cover, bool focused);

#endif
