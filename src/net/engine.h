/* Audio engine: a high-priority thread that decodes the current book, applies
 * speed (sonic) and volume, and writes PCM to a blocking sink. The UI talks to
 * it with commands and reads back state and position. */
#ifndef ABSVITA_ENGINE_H
#define ABSVITA_ENGINE_H
#include <stdbool.h>
#include <stdint.h>

#include "models.h"
#include "stream.h"

typedef enum { ENG_IDLE, ENG_LOADING, ENG_PLAYING, ENG_PAUSED, ENG_BUFFERING, ENG_ENDED, ENG_ERROR } EngineState;

/* Where PCM goes. write blocks until the device accepts the frames (that is the
 * engine's clock). open is called again whenever the format changes. */
typedef struct {
    int (*open)(void *ud, int rate, int channels);
    int (*write)(void *ud, const int16_t *pcm, int frames);
    void (*close)(void *ud);
    void *ud;
} AudioSink;

typedef struct {
    int ntracks;
    Track *tracks;          /* copied by engine_load */
    TrackSource *sources;   /* one per track, copied */
    double duration;
} PlayableBook;

int engine_init(const AudioSink *sink, AuthFn auth);
void engine_exit(void);

void engine_load(const PlayableBook *b, double start_t, bool autoplay);
void engine_stop(void);
void engine_play(void);
void engine_pause(void);
void engine_seek(double t);
void engine_set_speed(float speed);
void engine_set_volume(float v);  /* 0..1, applied to the samples */

EngineState engine_state(void);
bool engine_is_playing(void);     /* playing or buffering with the intent to play */
double engine_position(void);     /* book seconds */
double engine_duration(void);
float engine_cpu_load(void);      /* share of wall time spent decoding, 0..1 */
int engine_error(void);           /* STREAM_* code for ENG_ERROR */
int engine_net_state(void);       /* HSRC_* for the current track */
float engine_speed(void);

#endif
