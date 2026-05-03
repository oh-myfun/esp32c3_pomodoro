#pragma once

#include <stdint.h>

#define MAX_ANIM_FRAMES  4
#define BUDDY_STATE_COUNT 7
#define BUDDY_FRAME_LINES 12

typedef enum {
    BUDDY_SLEEP = 0,
    BUDDY_IDLE,
    BUDDY_BUSY,
    BUDDY_ATTENTION,
    BUDDY_CELEBRATE,
    BUDDY_DIZZY,
    BUDDY_HEART,
} buddy_state_t;

typedef struct {
    const char *name;
    const char *personality;
    const char *const (*state_frames[BUDDY_STATE_COUNT])[MAX_ANIM_FRAMES][BUDDY_FRAME_LINES];
    uint8_t frame_count[BUDDY_STATE_COUNT];
} buddy_species_t;

extern const buddy_species_t BUDDY_SPECIES[];
extern const int BUDDY_SPECIES_COUNT;
