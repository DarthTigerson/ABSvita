/* Buttons (with key repeat and the left stick as a d-pad) and front-touch gestures. */
#ifndef ABSVITA_INPUT_H
#define ABSVITA_INPUT_H
#include <stdbool.h>
#include <stdint.h>

enum {
    B_UP = 1 << 0, B_DOWN = 1 << 1, B_LEFT = 1 << 2, B_RIGHT = 1 << 3,
    B_OK = 1 << 4,       /* confirm: X or O depending on the system setting */
    B_BACK = 1 << 5,     /* the other one */
    B_SQUARE = 1 << 6, B_TRIANGLE = 1 << 7,
    B_L = 1 << 8, B_R = 1 << 9, B_START = 1 << 10, B_SELECT = 1 << 11,
};

typedef struct {
    uint32_t down;     /* pressed this frame */
    uint32_t held;
    uint32_t repeat;   /* down + auto-repeat for directions and L/R */
    bool any;          /* any button pressed this frame */
    /* touch (screen pixels) */
    bool touching;
    float tx, ty;      /* current touch point */
    bool tap;          /* a short touch without movement ended this frame */
    float tap_x, tap_y;
    bool dragging;     /* moved past the slop */
    float drag_dx, drag_dy; /* movement since the last frame while dragging */
    float start_x, start_y;
} Input;

void input_init(bool enter_is_cross);
void input_poll(Input *in);

#endif
