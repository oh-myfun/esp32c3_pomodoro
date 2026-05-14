#pragma once

#include <stdint.h>

#define BUDDY_STATE_COUNT  7
#define BUDDY_FRAME_LINES  5
#define BUDDY_FRAME_COLS   12
#define MAX_ANIM_FRAMES   10
#define MAX_SEQ_LEN       48

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
    uint16_t body_color;
    const char *const (*state_frames[BUDDY_STATE_COUNT])[MAX_ANIM_FRAMES][BUDDY_FRAME_LINES];
    const uint8_t *seq[BUDDY_STATE_COUNT];       // SEQ array: tick -> pose index
    uint8_t seq_len[BUDDY_STATE_COUNT];           // SEQ array length
    uint8_t pose_count[BUDDY_STATE_COUNT];        // number of unique poses
} buddy_species_t;

extern const buddy_species_t BUDDY_SPECIES[];
extern const int BUDDY_SPECIES_COUNT;
