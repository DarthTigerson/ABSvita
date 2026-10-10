/* Content views and full-screen screens. */
#ifndef ABSVITA_VIEWS_H
#define ABSVITA_VIEWS_H
#include "app.h"
#include "groups.h"

View *view_home_new(void);
View *view_library_new(void);
View *view_groups_new(GroupKind kind);
/* a fixed list of books (a series or an author); idx is copied */
View *view_books_new(const char *title, const char *subtitle, const int *idx, int n, bool numbered, const double *seq);
View *view_search_new(bool open_keyboard);
View *view_settings_new(void);
View *view_book_new(const BookSummary *b); /* copied */
View *view_downloads_new(void);

/* Now Playing (full screen) */
void player_screen_enter(void);
void player_screen_update(const Input *in, float dt);
void player_screen_draw(void);

/* Login (full screen) */
void login_enter(const char *message);
void login_update(const Input *in, float dt);
void login_draw(void);

#endif
