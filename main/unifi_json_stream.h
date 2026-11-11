#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Bounded incremental JSON SAX reader. No allocation or response accumulation. */
typedef enum { UJSON_OBJECT, UJSON_ARRAY, UJSON_END_OBJECT, UJSON_END_ARRAY,
               UJSON_STRING, UJSON_NUMBER, UJSON_BOOL, UJSON_NULL } ujson_event_t;
typedef bool (*ujson_callback_t)(void *, ujson_event_t, const char *, const char *);
typedef struct {
    char segment[64], key[64];
    unsigned index;
    uint8_t state;
    bool array;
} ujson_frame_t;
typedef struct {
    ujson_frame_t frames[20];
    char token[512], path[512];
    size_t used;
    uint32_t unicode, high_surrogate;
    unsigned depth, unicode_digits;
    uint8_t mode;
    bool key_token, escape, failed, done, started;
    ujson_callback_t callback;
    void *user;
} ujson_stream_t;
void ujson_init(ujson_stream_t *, ujson_callback_t, void *);
bool ujson_feed(ujson_stream_t *, const char *, size_t);
bool ujson_finish(ujson_stream_t *);
