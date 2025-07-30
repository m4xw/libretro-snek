/*
 * Snek libretro core
 *
 * This file contains a complete libretro core implementing a modern
 * reimagining of the classic Snake game. The goal of this project
 * is to provide a small yet polished example of a standalone game
 * using the libretro API.
 */
#include "libretro.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <stdint.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

/* ------------------------------------------------------------------
 * Configuration constants
 *
 * Adjust these values to tune the game’s behaviour. GRID_W and
 * GRID_H determine how many cells are available in the play field.
 * CELL_SIZE defines the pixel dimension of a single cell. The
 * resolution of the framebuffer is derived from these. A higher
 * resolution allows for smoother shading at the cost of more memory.
 */

#define GRID_W 40
#define GRID_H 30
#define CELL_SIZE 16
/* Derived framebuffer dimensions. */
#define FB_WIDTH (GRID_W * CELL_SIZE)
#define FB_HEIGHT (GRID_H * CELL_SIZE) /* Extra space at top for HUD */

/* Maximum snake length. In practice this can be GRID_W * GRID_H but
 * we set a reasonable cap to avoid dynamic allocations at runtime. */
#define MAX_SNAKE_LENGTH (GRID_W * GRID_H)

/* Particle system configuration. Up to this many particles can be
 * active at once. Each collected item spawns a handful of particles. */
#define MAX_PARTICLES 128

/* Power‑up durations (in frames). 60 frames ≈ 1 second at 60 Hz. */
#define PHASE_DURATION (60 * 5) /* 5 seconds of phasing */
#define SPEED_DURATION (60 * 5) /* 5 seconds of speed boost */

/* Movement speed. Lower values produce a faster snake because the
 * snake advances once every MOVE_INTERVAL frames. A speed boost will
 * divide this interval by two. */
#define BASE_MOVE_INTERVAL 8

/* Probability that a power‑up is spawned when food is consumed.
 * Expressed as a fraction of RAND_MAX. For example 0.2 means
 * 20 percent chance. */
#define POWERUP_PROBABILITY 0.5f

/* Colours used in the game. Encoded as 0xAARRGGBB but since we
 * request XRGB8888 from the frontend the high byte (alpha) is
 * ignored. */
typedef uint32_t colour_t;
#define RGB(r, g, b)                   \
    ((((uint32_t)(r) & 0xFFu) << 16) | \
     ((uint32_t)(g) & 0xFFu) << 8) |   \
        ((uint32_t)(b) & 0xFFu)

static const colour_t BG_COLOUR_TOP = RGB(30, 30, 40);
static const colour_t BG_COLOUR_BOTTOM = RGB(10, 10, 20);
static const colour_t SNAKE_HEAD_COLOUR = RGB(200, 200, 40);
static const colour_t SNAKE_BODY_COLOUR = RGB(80, 200, 80);
static const colour_t FOOD_COLOUR = RGB(200, 80, 80);
static const colour_t PHASE_COLOUR = RGB(80, 80, 200);
static const colour_t SPEED_COLOUR = RGB(200, 160, 40);
static const colour_t HUD_TEXT_COLOUR = RGB(240, 240, 240);
static const colour_t GAMEOVER_COLOUR = RGB(255, 60, 60);

/* Segment definitions for seven‑segment display. Each bit in the
 * 7‑bit mask represents a segment a–g as follows:
 *  0babcdefg (bit 6 is a, bit 0 is g). */
static const uint8_t seven_seg_digits[10] = {
    /* 0 */ 0b1111110,
    /* 1 */ 0b0110000,
    /* 2 */ 0b1101101,
    /* 3 */ 0b1111001,
    /* 4 */ 0b0110011,
    /* 5 */ 0b1011011,
    /* 6 */ 0b1011111,
    /* 7 */ 0b1110000,
    /* 8 */ 0b1111111,
    /* 9 */ 0b1111011};

/* Basic 8×8 bitmap font for a handful of capital letters. Each row
 * contains one byte; bits set to 1 indicate lit pixels. Only the
 * glyphs needed for HUD messages are defined. */
typedef struct
{
    char ch;
    uint8_t bitmap[8];
} glyph_t;

static const glyph_t font_glyphs[] = {
    {'A', {0x38, 0x44, 0x44, 0x7c, 0x44, 0x44, 0x44, 0x00}},
    {'B', {0x78, 0x44, 0x44, 0x78, 0x44, 0x44, 0x78, 0x00}},
    {'C', {0x38, 0x44, 0x40, 0x40, 0x40, 0x44, 0x38, 0x00}},
    {'D', {0x78, 0x44, 0x44, 0x44, 0x44, 0x44, 0x78, 0x00}},
    {'E', {0x7c, 0x40, 0x40, 0x78, 0x40, 0x40, 0x7c, 0x00}},
    {'F', {0x7c, 0x40, 0x40, 0x78, 0x40, 0x40, 0x40, 0x00}},
    {'G', {0x38, 0x44, 0x40, 0x5c, 0x44, 0x44, 0x38, 0x00}},
    {'H', {0x44, 0x44, 0x44, 0x7c, 0x44, 0x44, 0x44, 0x00}},
    {'I', {0x3c, 0x10, 0x10, 0x10, 0x10, 0x10, 0x3c, 0x00}},
    {'J', {0x1c, 0x08, 0x08, 0x08, 0x08, 0x48, 0x30, 0x00}},
    {'K', {0x44, 0x48, 0x50, 0x60, 0x50, 0x48, 0x44, 0x00}},
    {'L', {0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x7c, 0x00}},
    {'M', {0x44, 0x6c, 0x54, 0x54, 0x44, 0x44, 0x44, 0x00}},
    {'N', {0x44, 0x64, 0x54, 0x4c, 0x44, 0x44, 0x44, 0x00}},
    {'O', {0x38, 0x44, 0x44, 0x44, 0x44, 0x44, 0x38, 0x00}},
    {'P', {0x78, 0x44, 0x44, 0x78, 0x40, 0x40, 0x40, 0x00}},
    {'Q', {0x38, 0x44, 0x44, 0x44, 0x54, 0x48, 0x34, 0x00}},
    {'R', {0x78, 0x44, 0x44, 0x78, 0x50, 0x48, 0x44, 0x00}},
    {'S', {0x38, 0x44, 0x20, 0x18, 0x04, 0x44, 0x38, 0x00}},
    {'T', {0x7c, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x00}},
    {'U', {0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x38, 0x00}},
    {'V', {0x44, 0x44, 0x44, 0x44, 0x44, 0x28, 0x10, 0x00}},
    {'W', {0x44, 0x44, 0x44, 0x54, 0x54, 0x6c, 0x44, 0x00}},
    {'X', {0x44, 0x44, 0x28, 0x10, 0x28, 0x44, 0x44, 0x00}},
    {'Y', {0x44, 0x44, 0x28, 0x10, 0x10, 0x10, 0x3c, 0x00}},
    {'Z', {0x7c, 0x04, 0x08, 0x10, 0x20, 0x40, 0x7c, 0x00}},
    {'0', {0x38, 0x44, 0x4c, 0x54, 0x64, 0x44, 0x38, 0x00}},
    {'1', {0x10, 0x30, 0x10, 0x10, 0x10, 0x10, 0x38, 0x00}},
    {'2', {0x38, 0x44, 0x04, 0x08, 0x10, 0x20, 0x7c, 0x00}},
    {'3', {0x38, 0x44, 0x04, 0x18, 0x04, 0x44, 0x38, 0x00}},
    {'4', {0x08, 0x18, 0x28, 0x48, 0x7c, 0x08, 0x08, 0x00}},
    {'5', {0x7c, 0x40, 0x78, 0x04, 0x04, 0x44, 0x38, 0x00}},
    {'6', {0x18, 0x20, 0x40, 0x78, 0x44, 0x44, 0x38, 0x00}},
    {'7', {0x7c, 0x04, 0x08, 0x10, 0x20, 0x20, 0x20, 0x00}},
    {'8', {0x38, 0x44, 0x44, 0x38, 0x44, 0x44, 0x38, 0x00}},
    {'9', {0x38, 0x44, 0x44, 0x3c, 0x04, 0x08, 0x30, 0x00}}};

/* Helper to retrieve a glyph bitmap for a character. Returns NULL if
 * the character is undefined; undefined characters simply aren’t
 * drawn. */
static const uint8_t *get_glyph_bitmap(char c)
{
    size_t n = sizeof(font_glyphs) / sizeof(font_glyphs[0]);
    for (size_t i = 0; i < n; ++i)
    {
        if (font_glyphs[i].ch == c)
            return font_glyphs[i].bitmap;
    }
    return NULL;
}

/* Game state enumeration. */
typedef enum
{
    STATE_TITLE,
    STATE_PLAY,
    STATE_PAUSE,
    STATE_GAMEOVER
} game_state_t;

/* Directions used by the snake. */
typedef enum
{
    DIR_UP,
    DIR_DOWN,
    DIR_LEFT,
    DIR_RIGHT
} direction_t;

/* Power‑up types. */
typedef enum
{
    ITEM_NONE = 0,
    ITEM_PHASE,
    ITEM_SPEED
} item_type_t;

/* Particle structure for simple explosion effects. */
typedef struct
{
    float x, y;
    float vx, vy;
    int lifetime;
    colour_t colour;
    bool active;
} particle_t;

/* Global video buffer. Each pixel uses 32‑bit XRGB8888. */
static colour_t *video_buffer = NULL;
static size_t video_pitch = 0;

/* Snake body positions. head index at 0, tail at length-1. */
static int snake_x[MAX_SNAKE_LENGTH];
static int snake_y[MAX_SNAKE_LENGTH];
static int snake_length = 0;
static direction_t snake_dir = DIR_RIGHT;
static direction_t pending_dir = DIR_RIGHT;

/* Fruit and power‑up positions. If item_type is ITEM_NONE no power‑up
 * is present. */
static int food_x, food_y;
static item_type_t item_type = ITEM_NONE;
static int item_x, item_y;

/* Timers for active power‑ups. When >0 the effect is active. */
static int phase_timer = 0;
static int speed_timer = 0;

/* Particle pool. */
static particle_t particles[MAX_PARTICLES];

/* Add obstacle grid and collision detection */
static int obstacle[GRID_W][GRID_H];
static int obstacle_count = 0;

/* Current score and high score. */
static int score = 0;
static int highscore = 0;

/* Game state and frame counters. move_counter counts down to the
 * snake’s next step. frame_count increments every call to retro_run
 * and is used for animations. */
static game_state_t state = STATE_TITLE;
static int move_counter = BASE_MOVE_INTERVAL;
static unsigned long frame_count = 0;

/* Libretro callback pointers set by the frontend. */
static retro_environment_t env_cb;
static retro_video_refresh_t video_cb;
static retro_audio_sample_t audio_cb;
static retro_audio_sample_batch_t audio_batch_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;

/* Forward declarations of internal functions. */
static void game_reset(void);
static void spawn_food(void);
static void spawn_item(void);
static void update_snake(void);
static void draw_frame(void);
static void draw_cell(int cx, int cy, colour_t colour, bool shaded);
static void draw_snake(void);
static void draw_food(void);
static void draw_item(void);
static void draw_scoreboard(void);
static void draw_text(int x, int y, const char *text, colour_t colour);
static void draw_digit(int x, int y, int value, colour_t colour);
static void draw_gameover_overlay(void);
static void update_particles(void);
static void spawn_particles(int cx, int cy, colour_t colour);
static void spawn_obstacles(void);
static void draw_obstacles(void);
static bool check_obstacle_collision(int new_x, int new_y);

/* Utility to compute a linear interpolation between two colours. */
static inline colour_t lerp_colour(colour_t a, colour_t b, float t)
{
    uint8_t ar = (a >> 16) & 0xFF;
    uint8_t ag = (a >> 8) & 0xFF;
    uint8_t ab = a & 0xFF;
    uint8_t br = (b >> 16) & 0xFF;
    uint8_t bg = (b >> 8) & 0xFF;
    uint8_t bb = b & 0xFF;
    uint8_t r = (uint8_t)((1.f - t) * ar + t * br);
    uint8_t g = (uint8_t)((1.f - t) * ag + t * bg);
    uint8_t b2 = (uint8_t)((1.f - t) * ab + t * bb);
    return (r << 16) | (g << 8) | b2;
}

/* Initialise a new game. Resets the snake, spawns food and resets
 * timers and counters. */
static void game_reset(void)
{
    snake_length = 3;
    snake_x[0] = GRID_W / 2;
    snake_y[0] = GRID_H / 2;
    snake_x[1] = snake_x[0] - 1;
    snake_y[1] = snake_y[0];
    snake_x[2] = snake_x[1] - 1;
    snake_y[2] = snake_y[1];
    snake_dir = DIR_RIGHT;
    pending_dir = DIR_RIGHT;
    score = 0;
    phase_timer = 0;
    speed_timer = 0;
    item_type = ITEM_NONE;
    move_counter = BASE_MOVE_INTERVAL;
    frame_count = 0;
    /* deactivate particles */
    for (int i = 0; i < MAX_PARTICLES; i++)
        particles[i].active = false;
    // Reset obstacles
    for (int x = 0; x < GRID_W; x++)
        for (int y = 0; y < GRID_H; y++)
            obstacle[x][y] = 0;
    obstacle_count = 0;
    spawn_food();
    spawn_obstacles();
}

/* Generate a random cell coordinate that does not collide with the
 * snake. This helper loops until a free cell is found. */
static void random_free_cell(int *out_x, int *out_y)
{
    while (1)
    {
        int x = rand() % GRID_W;
        int y = rand() % GRID_H;
        bool collides = false;
        // Check snake collision
        for (int i = 0; i < snake_length; i++)
        {
            if (snake_x[i] == x && snake_y[i] == y)
            {
                collides = true;
                break;
            }
        }
        // Check obstacle collision
        if (!collides && obstacle[x][y])
            collides = true;
        // Check item collision
        if (!collides && item_type != ITEM_NONE && item_x == x && item_y == y)
            collides = true;
        if (!collides)
        {
            *out_x = x;
            *out_y = y;
            return;
        }
    }
}

/* Spawn food at a random free location. */
static void spawn_food(void)
{
    random_free_cell(&food_x, &food_y);
}

/* Spawn a power‑up with a random type and position. Only spawn if
 * none is currently active. The chance of spawning is governed by
 * POWERUP_PROBABILITY. */
static void spawn_item(void)
{
    if (item_type != ITEM_NONE)
        return;
    if (((float)rand() / (float)RAND_MAX) > POWERUP_PROBABILITY)
        return;
    /* Decide which item to spawn. We currently choose between
     * phasing and speed boost with equal probability. */
    item_type = (rand() % 2) ? ITEM_PHASE : ITEM_SPEED;
    random_free_cell(&item_x, &item_y);
}

/* Spawn an explosion of particles at a given cell. */
static void spawn_particles(int cx, int cy, colour_t colour)
{
    /* Convert cell coordinates to pixel centre. */
    float px = (float)cx * CELL_SIZE + CELL_SIZE / 2.0f;
    float py = (float)cy * CELL_SIZE + CELL_SIZE / 2.0f;
    for (int i = 0; i < MAX_PARTICLES; i++)
    {
        if (!particles[i].active)
        {
            particles[i].active = true;
            particles[i].x = px;
            particles[i].y = py;
            float angle = (float)rand() / (float)RAND_MAX * 2.0f * (float)M_PI;
            float speed = 0.5f + ((float)rand() / (float)RAND_MAX) * 1.5f;
            particles[i].vx = cosf(angle) * speed;
            particles[i].vy = sinf(angle) * speed;
            particles[i].lifetime = 30 + rand() % 30;
            particles[i].colour = colour;
            /* spawn only a handful per cell */
            if (rand() % 2 == 0)
                break;
        }
    }
}

/* Update all active particles. Moves them and decrements lifetime. */
static void update_particles(void)
{
    for (int i = 0; i < MAX_PARTICLES; i++)
    {
        if (particles[i].active)
        {
            particles[i].x += particles[i].vx;
            particles[i].y += particles[i].vy;
            particles[i].lifetime--;
            if (particles[i].lifetime <= 0)
            {
                particles[i].active = false;
                continue;
            }
            /* Fade out: make colour darker over time. */
            float t = (float)particles[i].lifetime / 60.0f;
            uint8_t r = (particles[i].colour >> 16) & 0xFF;
            uint8_t g = (particles[i].colour >> 8) & 0xFF;
            uint8_t b = particles[i].colour & 0xFF;
            r = (uint8_t)(r * t);
            g = (uint8_t)(g * t);
            b = (uint8_t)(b * t);
            particles[i].colour = (r << 16) | (g << 8) | b;
        }
    }
}

/* Add obstacle grid and collision detection */
static int obstacle[GRID_W][GRID_H];

/* Spawn obstacles at random positions */
static void spawn_obstacles(void)
{
    // Set border cells as obstacles
    for (int x = 0; x < GRID_W; x++)
    {
        obstacle[x][0] = 1;
        obstacle[x][GRID_H - 1] = 1;
    }
    for (int y = 0; y < GRID_H; y++)
    {
        obstacle[0][y] = 1;
        obstacle[GRID_W - 1][y] = 1;
    }
    obstacle_count = 2 * (GRID_W + GRID_H) - 4;
    // Optionally, you can still spawn random obstacles inside the border if desired
    int num_obstacles = GRID_W * GRID_H / 100;
    for (int i = 0; i < num_obstacles; i++)
    {
        int x, y;
        do
        {
            x = rand() % GRID_W;
            y = rand() % GRID_H;
        } while (
            (snake_x[0] == x && snake_y[0] == y) ||
            (food_x == x && food_y == y) ||
            (item_type != ITEM_NONE && item_x == x && item_y == y) ||
            obstacle[x][y]);
        obstacle[x][y] = 1;
        obstacle_count++;
    }
}

/* Check for obstacle collisions */
static bool check_obstacle_collision(int new_x, int new_y)
{
    return (new_x >= 0 && new_x < GRID_W && new_y >= 0 && new_y < GRID_H && obstacle[new_x][new_y]);
}

/* Draw obstacles on the grid */
// Fancy pixel art for obstacles: stone block with cracks and highlights
static void draw_obstacle_pixelart(int cx, int cy)
{
    int px = cx * CELL_SIZE;
    int py = cy * CELL_SIZE;
    for (int y = 0; y < CELL_SIZE; y++)
    {
        for (int x = 0; x < CELL_SIZE; x++)
        {
            // Base stone color with vertical gradient
            float t = (float)y / (float)CELL_SIZE;
            uint8_t r = 110 + (uint8_t)(30 * t);
            uint8_t g = 110 + (uint8_t)(30 * t);
            uint8_t b = 110 + (uint8_t)(30 * t);
            // Edge highlight
            if (x < 2 || y < 2)
            {
                r = (uint8_t)(r + 40);
                g = (uint8_t)(g + 40);
                b = (uint8_t)(b + 40);
            }
            // Shadow bottom/right
            if (x > CELL_SIZE - 3 || y > CELL_SIZE - 3)
            {
                r = (uint8_t)(r * 0.7f);
                g = (uint8_t)(g * 0.7f);
                b = (uint8_t)(b * 0.7f);
            }
            // Random speckles for stone texture
            if (((x * y + cx * 13 + cy * 7) % 17) == 0)
            {
                r = (uint8_t)(r * 0.8f);
                g = (uint8_t)(g * 0.8f);
                b = (uint8_t)(b * 0.8f);
            }
            // Cracks: draw a few dark lines
            if ((x == CELL_SIZE / 2 && y > CELL_SIZE / 4) || (y == CELL_SIZE / 2 && x > CELL_SIZE / 4))
            {
                r = (uint8_t)(r * 0.4f);
                g = (uint8_t)(g * 0.4f);
                b = (uint8_t)(b * 0.4f);
            }
            // Occasional extra crack
            if ((x == y && x > 3 && x < CELL_SIZE - 3))
            {
                r = (uint8_t)(r * 0.5f);
                g = (uint8_t)(g * 0.5f);
                b = (uint8_t)(b * 0.5f);
            }
            int draw_x = px + x;
            int draw_y = py + y;
            if (draw_x >= 0 && draw_x < FB_WIDTH && draw_y >= 0 && draw_y < FB_HEIGHT)
                video_buffer[draw_y * FB_WIDTH + draw_x] = (r << 16) | (g << 8) | b;
        }
    }
}

static void draw_obstacles(void)
{
    for (int x = 0; x < GRID_W; x++)
    {
        for (int y = 0; y < GRID_H; y++)
        {
            if (obstacle[x][y])
            {
                draw_obstacle_pixelart(x, y);
            }
        }
    }
}

/* Update snake with obstacle collision detection */
static void update_snake(void)
{
    /* Apply pending direction at the start of the move. */
    snake_dir = pending_dir;
    /* Compute new head position based on direction. */
    int new_x = snake_x[0];
    int new_y = snake_y[0];
    switch (snake_dir)
    {
    case DIR_UP:
        new_y--;
        break;
    case DIR_DOWN:
        new_y++;
        break;
    case DIR_LEFT:
        new_x--;
        break;
    case DIR_RIGHT:
        new_x++;
        break;
    }

    /* Check wall collisions. If the snake hits a wall and phasing is
     * inactive the game is over. With phasing active we simply wrap
     * around to the other side. */
    bool wrap = (phase_timer > 0);
    if (wrap)
    {
        if (new_x < 0)
            new_x = GRID_W - 1;
        if (new_x >= GRID_W)
            new_x = 0;
        if (new_y < 0)
            new_y = GRID_H - 1;
        if (new_y >= GRID_H)
            new_y = 0;
    }
    else
    {
        if (new_x < 0 || new_x >= GRID_W || new_y < 0 || new_y >= GRID_H)
        {
            state = STATE_GAMEOVER;
            return;
        }
    }

    /* Check self collision. With phasing active we ignore collisions
     * with the body. */
    if (phase_timer <= 0)
    {
        for (int i = 0; i < snake_length; i++)
        {
            if (snake_x[i] == new_x && snake_y[i] == new_y)
            {
                state = STATE_GAMEOVER;
                return;
            }
        }
    }

    /* Check for obstacle collision */
    if (check_obstacle_collision(new_x, new_y))
    {
        state = STATE_GAMEOVER;
        return;
    }

    /* Move body: shift positions down the array. We iterate from
     * tail to head so we don’t overwrite data. */
    for (int i = snake_length - 1; i > 0; i--)
    {
        snake_x[i] = snake_x[i - 1];
        snake_y[i] = snake_y[i - 1];
    }
    snake_x[0] = new_x;
    snake_y[0] = new_y;

    /* Check fruit collision. If we eat food we grow by one segment. */
    if (new_x == food_x && new_y == food_y)
    {
        if (snake_length < MAX_SNAKE_LENGTH)
        {
            snake_length++;
            snake_x[snake_length - 1] = snake_x[snake_length - 2];
            snake_y[snake_length - 1] = snake_y[snake_length - 2];
        }
        score += 10;
        if (score > highscore)
            highscore = score;
        spawn_particles(food_x, food_y, FOOD_COLOUR);
        spawn_food();
        spawn_item();
    }

    /* Check item collision. Activate power‑up and remove item. */
    if (item_type != ITEM_NONE && new_x == item_x && new_y == item_y)
    {
        if (item_type == ITEM_PHASE)
        {
            phase_timer = PHASE_DURATION;
        }
        else if (item_type == ITEM_SPEED)
        {
            speed_timer = SPEED_DURATION;
        }
        spawn_particles(item_x, item_y,
                        (item_type == ITEM_PHASE ? PHASE_COLOUR : SPEED_COLOUR));
        item_type = ITEM_NONE;
    }
}

/* Poll input and update direction or state accordingly. */
static void handle_input(void)
{
    input_poll_cb();
    int up = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP);
    int down = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN);
    int left = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT);
    int right = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT);
    int start = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START);
    int select = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT);

    /* Toggle pause when pressing Start. In title screen, pressing
     * Start begins the game. After game over pressing Start resets
     * and begins a new game. */
    static int prev_start = 0;
    if (start && !prev_start)
    {
        if (state == STATE_TITLE)
        {
            state = STATE_PLAY;
            game_reset();
        }
        else if (state == STATE_PLAY)
        {
            state = STATE_PAUSE;
        }
        else if (state == STATE_PAUSE)
        {
            state = STATE_PLAY;
        }
        else if (state == STATE_GAMEOVER)
        {
            state = STATE_PLAY;
            game_reset();
        }
    }
    prev_start = start;

    /* Reset the highscore with Select if on the title screen. */
    static int prev_select = 0;
    if (select && !prev_select)
    {
        if (state == STATE_TITLE)
        {
            highscore = 0;
        }
    }
    prev_select = select;

    /* Only allow turning when in play state. The snake cannot
     * reverse direction. Only update pending_dir, not snake_dir. */
    if (state == STATE_PLAY)
    {
        if (up && snake_dir != DIR_DOWN && pending_dir != DIR_DOWN)
            pending_dir = DIR_UP;
        else if (down && snake_dir != DIR_UP && pending_dir != DIR_UP)
            pending_dir = DIR_DOWN;
        else if (left && snake_dir != DIR_RIGHT && pending_dir != DIR_RIGHT)
            pending_dir = DIR_LEFT;
        else if (right && snake_dir != DIR_LEFT && pending_dir != DIR_LEFT)
            pending_dir = DIR_RIGHT;
    }
}

/* Draw a 3D‑shaded square representing a cell. When shaded==true
 * the cell interior is filled with the supplied colour and the
 * edges are lightened/darkened to produce a beveled effect. When
 * shaded==false the cell is drawn solid with the given colour. */
static void draw_cell(int cx, int cy, colour_t colour, bool shaded)
{
    int px = cx * CELL_SIZE;
    int py = cy * CELL_SIZE;
    for (int y = 0; y < CELL_SIZE; y++)
    {
        for (int x = 0; x < CELL_SIZE; x++)
        {
            float fx = (float)x / (float)CELL_SIZE;
            float fy = (float)y / (float)CELL_SIZE;
            colour_t col = colour;
            if (shaded)
            {
                /* Lighten top/left edges and darken bottom/right edges. */
                float shade = 0.0f;
                if (fx < 0.1f || fy < 0.1f)
                    shade = 0.2f;
                else if (fx > 0.9f || fy > 0.9f)
                    shade = -0.2f;
                uint8_t r = (col >> 16) & 0xFF;
                uint8_t g = (col >> 8) & 0xFF;
                uint8_t b = col & 0xFF;
                int ir = (int)r + (int)(shade * 255.f);
                int ig = (int)g + (int)(shade * 255.f);
                int ib = (int)b + (int)(shade * 255.f);
                if (ir < 0)
                    ir = 0;
                if (ir > 255)
                    ir = 255;
                if (ig < 0)
                    ig = 0;
                if (ig > 255)
                    ig = 255;
                if (ib < 0)
                    ib = 0;
                if (ib > 255)
                    ib = 255;
                col = (ir << 16) | (ig << 8) | ib;
            }
            int draw_x = px + x;
            int draw_y = py + y;
            if (draw_x >= 0 && draw_x < FB_WIDTH && draw_y >= 0 && draw_y < FB_HEIGHT)
            {
                video_buffer[draw_y * FB_WIDTH + draw_x] = col;
            }
        }
    }
}

/* Draw the snake body. The head uses a distinct colour. Body
 * segments gradually darken towards the tail to give a subtle
 * gradient effect. During phasing the snake is tinted with the
 * phasing colour. */
// Fancy pixel art for snake head
static void draw_snake_head(int cx, int cy, direction_t dir, colour_t base, bool phasing)
{
    int px = cx * CELL_SIZE;
    int py = cy * CELL_SIZE;
    // Draw a rounded head with a highlight and a mouth
    for (int y = 0; y < CELL_SIZE; y++)
    {
        for (int x = 0; x < CELL_SIZE; x++)
        {
            // Circle mask for head
            int dx = x - CELL_SIZE / 2;
            int dy = y - CELL_SIZE / 2;
            if (dx * dx + dy * dy < (CELL_SIZE / 2) * (CELL_SIZE / 2))
            {
                float t = 0.7f + 0.3f * (float)(CELL_SIZE / 2 - dy) / (CELL_SIZE / 2); // vertical gradient
                uint8_t r = (base >> 16) & 0xFF;
                uint8_t g = (base >> 8) & 0xFF;
                uint8_t b = base & 0xFF;
                r = (uint8_t)(r * t);
                g = (uint8_t)(g * t);
                b = (uint8_t)(b * t);
                colour_t col = (r << 16) | (g << 8) | b;
                // Highlight
                if (x < CELL_SIZE / 2 && y < CELL_SIZE / 2 && dx * dx + dy * dy < (CELL_SIZE / 2 - 2) * (CELL_SIZE / 2 - 2))
                    col = lerp_colour(col, RGB(255, 255, 255), 0.15f);
                // Phasing tint (keep for visual effect, but now color is handled in draw_snake)
                if (phasing)
                    col = lerp_colour(col, PHASE_COLOUR, 0.2f);
                int draw_x = px + x;
                int draw_y = py + y;
                if (draw_x >= 0 && draw_x < FB_WIDTH && draw_y >= 0 && draw_y < FB_HEIGHT)
                    video_buffer[draw_y * FB_WIDTH + draw_x] = col;
            }
        }
    }
    // Eyes
    int ex1, ey1, ex2, ey2;
    switch (dir)
    {
    case DIR_UP:
        ex1 = px + CELL_SIZE / 3;
        ey1 = py + CELL_SIZE / 4;
        ex2 = px + 2 * CELL_SIZE / 3;
        ey2 = py + CELL_SIZE / 4;
        break;
    case DIR_DOWN:
        ex1 = px + CELL_SIZE / 3;
        ey1 = py + 3 * CELL_SIZE / 4;
        ex2 = px + 2 * CELL_SIZE / 3;
        ey2 = py + 3 * CELL_SIZE / 4;
        break;
    case DIR_LEFT:
        ex1 = px + CELL_SIZE / 4;
        ey1 = py + CELL_SIZE / 3;
        ex2 = px + CELL_SIZE / 4;
        ey2 = py + 2 * CELL_SIZE / 3;
        break;
    case DIR_RIGHT:
    default:
        ex1 = px + 3 * CELL_SIZE / 4;
        ey1 = py + CELL_SIZE / 3;
        ex2 = px + 3 * CELL_SIZE / 4;
        ey2 = py + 2 * CELL_SIZE / 3;
        break;
    }
    for (int dy = 0; dy < 3; dy++)
    {
        for (int dx = 0; dx < 3; dx++)
        {
            int draw1_x = ex1 + dx - 1;
            int draw1_y = ey1 + dy - 1;
            int draw2_x = ex2 + dx - 1;
            int draw2_y = ey2 + dy - 1;
            if (draw1_x >= 0 && draw1_x < FB_WIDTH && draw1_y >= 0 && draw1_y < FB_HEIGHT)
                video_buffer[draw1_y * FB_WIDTH + draw1_x] = RGB(0, 0, 0);
            if (draw2_x >= 0 && draw2_x < FB_WIDTH && draw2_y >= 0 && draw2_y < FB_HEIGHT)
                video_buffer[draw2_y * FB_WIDTH + draw2_x] = RGB(0, 0, 0);
        }
    }
    // Mouth (small arc)
    int mx = px + CELL_SIZE / 2;
    int my = py + CELL_SIZE / 2 + 3;
    for (int i = -2; i <= 2; i++)
    {
        int draw_x = mx + i;
        int draw_y = my + (i * i) / 6;
        if (draw_x >= 0 && draw_x < FB_WIDTH && draw_y >= 0 && draw_y < FB_HEIGHT)
            video_buffer[draw_y * FB_WIDTH + draw_x] = RGB(60, 30, 0);
    }
}

// Fancy pixel art for snake body segment (scales/stripes)
static void draw_snake_body(int cx, int cy, colour_t base, float t, bool phasing)
{
    int px = cx * CELL_SIZE;
    int py = cy * CELL_SIZE;
    for (int y = 0; y < CELL_SIZE; y++)
    {
        for (int x = 0; x < CELL_SIZE; x++)
        {
            // Elliptical mask for body
            int dx = x - CELL_SIZE / 2;
            int dy = y - CELL_SIZE / 2;
            if ((dx * dx) * 3 / 4 + dy * dy < (CELL_SIZE / 2) * (CELL_SIZE / 2))
            {
                float darken = 0.7f + 0.3f * (1.0f - t);
                uint8_t r = (base >> 16) & 0xFF;
                uint8_t g = (base >> 8) & 0xFF;
                uint8_t b = base & 0xFF;
                r = (uint8_t)(r * darken);
                g = (uint8_t)(g * darken);
                b = (uint8_t)(b * darken);
                colour_t col = (r << 16) | (g << 8) | b;
                // Add stripes
                if ((y % 4 == 0) && (x > 2 && x < CELL_SIZE - 2))
                    col = lerp_colour(col, RGB(40, 120, 40), 0.3f);
                // Add scale dots
                if ((x + y) % 7 == 0)
                    col = lerp_colour(col, RGB(200, 255, 200), 0.1f);
                // Phasing tint (keep for visual effect, but now color is handled in draw_snake)
                if (phasing)
                    col = lerp_colour(col, PHASE_COLOUR, 0.2f);
                int draw_x = px + x;
                int draw_y = py + y;
                if (draw_x >= 0 && draw_x < FB_WIDTH && draw_y >= 0 && draw_y < FB_HEIGHT)
                    video_buffer[draw_y * FB_WIDTH + draw_x] = col;
            }
        }
    }
}

static void draw_snake(void)
{
    // Determine active powerup and color
    bool phasing = (phase_timer > 0);
    bool speeding = (speed_timer > 0);
    int blink_frames = 60; // 1 second at 60Hz
    bool blink = false;
    colour_t powerup_head = SNAKE_HEAD_COLOUR;
    colour_t powerup_body = SNAKE_BODY_COLOUR;
    if (phasing)
    {
        powerup_head = PHASE_COLOUR;
        powerup_body = PHASE_COLOUR;
        if (phase_timer <= blink_frames && (frame_count / 6) % 2 == 0)
        {
            blink = true;
        }
    }
    else if (speeding)
    {
        powerup_head = SPEED_COLOUR;
        powerup_body = SPEED_COLOUR;
        if (speed_timer <= blink_frames && (frame_count / 6) % 2 == 0)
        {
            blink = true;
        }
    }
    for (int i = 0; i < snake_length; i++)
    {
        float t = (snake_length > 1) ? (float)i / (float)(snake_length - 1) : 0.f;
        colour_t base = (i == 0) ? powerup_head : powerup_body;
        // Blink to default color if effect is expiring
        if (blink)
        {
            base = (i == 0) ? SNAKE_HEAD_COLOUR : SNAKE_BODY_COLOUR;
        }
        if (i == 0)
        {
            draw_snake_head(snake_x[i], snake_y[i], snake_dir, base, phasing);
        }
        else
        {
            draw_snake_body(snake_x[i], snake_y[i], base, t, phasing);
        }
    }
}

/* Draw the fruit as a filled square with shading. */
// Fancy pixel art for food (shiny apple)
static void draw_food(void)
{
    int px = food_x * CELL_SIZE;
    int py = food_y * CELL_SIZE;
    // Apple body
    for (int y = 0; y < CELL_SIZE; y++)
    {
        for (int x = 0; x < CELL_SIZE; x++)
        {
            int dx = x - CELL_SIZE / 2;
            int dy = y - CELL_SIZE / 2 + 2;
            if (dx * dx + dy * dy < (CELL_SIZE / 2 - 1) * (CELL_SIZE / 2 - 1))
            {
                float t = 0.8f + 0.2f * (float)(CELL_SIZE / 2 - dy) / (CELL_SIZE / 2);
                uint8_t r = (FOOD_COLOUR >> 16) & 0xFF;
                uint8_t g = (FOOD_COLOUR >> 8) & 0xFF;
                uint8_t b = FOOD_COLOUR & 0xFF;
                r = (uint8_t)(r * t);
                g = (uint8_t)(g * t * 0.9f);
                b = (uint8_t)(b * t * 0.9f);
                colour_t col = (r << 16) | (g << 8) | b;
                // Highlight
                if (x < CELL_SIZE / 2 && y < CELL_SIZE / 2 && dx * dx + dy * dy < (CELL_SIZE / 2 - 3) * (CELL_SIZE / 2 - 3))
                    col = lerp_colour(col, RGB(255, 255, 255), 0.18f);
                int draw_x = px + x;
                int draw_y = py + y;
                if (draw_x >= 0 && draw_x < FB_WIDTH && draw_y >= 0 && draw_y < FB_HEIGHT)
                    video_buffer[draw_y * FB_WIDTH + draw_x] = col;
            }
        }
    }
    // Apple stem
    for (int y = 0; y < 3; y++)
    {
        int draw_x = px + CELL_SIZE / 2;
        int draw_y = py + y + 2;
        if (draw_x >= 0 && draw_x < FB_WIDTH && draw_y >= 0 && draw_y < FB_HEIGHT)
            video_buffer[draw_y * FB_WIDTH + draw_x] = RGB(80, 40, 0);
    }
    // Apple leaf
    for (int y = 0; y < 2; y++)
    {
        for (int x = 0; x < 3; x++)
        {
            int draw_x = px + CELL_SIZE / 2 - 2 + x;
            int draw_y = py + 2 + y;
            if (draw_x >= 0 && draw_x < FB_WIDTH && draw_y >= 0 && draw_y < FB_HEIGHT)
                video_buffer[draw_y * FB_WIDTH + draw_x] = RGB(40, 180, 40);
        }
    }
}

/* Draw a power‑up icon. Phase is drawn as a diamond; speed as a
 * lightning bolt. */
// Fancy pixel art for power-ups: gem diamond and stylized lightning bolt
static void draw_item(void)
{
    if (item_type == ITEM_NONE)
        return;
    int px = item_x * CELL_SIZE;
    int py = item_y * CELL_SIZE;
    if (item_type == ITEM_PHASE)
    {
        // Gem-like diamond with facets and glow
        for (int y = 0; y < CELL_SIZE; y++)
        {
            for (int x = 0; x < CELL_SIZE; x++)
            {
                int dx = x - CELL_SIZE / 2;
                int dy = y - CELL_SIZE / 2;
                float dist = fabsf(dx) + fabsf(dy) * 0.9f;
                if (dist < CELL_SIZE / 2 - 1)
                {
                    float t = 0.7f + 0.3f * (float)(CELL_SIZE / 2 - dy) / (CELL_SIZE / 2);
                    uint8_t r = (PHASE_COLOUR >> 16) & 0xFF;
                    uint8_t g = (PHASE_COLOUR >> 8) & 0xFF;
                    uint8_t b = PHASE_COLOUR & 0xFF;
                    r = (uint8_t)(r * t);
                    g = (uint8_t)(g * t);
                    b = (uint8_t)(b * t);
                    colour_t col = (r << 16) | (g << 8) | b;
                    // Facet highlight
                    if ((dx > 0 && dy < 0) || (dx < 0 && dy < 0))
                        col = lerp_colour(col, RGB(200, 200, 255), 0.18f);
                    // Central shine
                    if (dx * dx + dy * dy < 9)
                        col = lerp_colour(col, RGB(255, 255, 255), 0.25f);
                    int draw_x = px + x;
                    int draw_y = py + y;
                    if (draw_x >= 0 && draw_x < FB_WIDTH && draw_y >= 0 && draw_y < FB_HEIGHT)
                        video_buffer[draw_y * FB_WIDTH + draw_x] = col;
                }
                // Outer glow
                else if (dist < CELL_SIZE / 2 + 1)
                {
                    int draw_x = px + x;
                    int draw_y = py + y;
                    colour_t col = lerp_colour(PHASE_COLOUR, RGB(255, 255, 255), 0.2f);
                    if (draw_x >= 0 && draw_x < FB_WIDTH && draw_y >= 0 && draw_y < FB_HEIGHT)
                        video_buffer[draw_y * FB_WIDTH + draw_x] = col;
                }
            }
        }
    }
    else if (item_type == ITEM_SPEED)
    {
        // Stylized lightning bolt with shading and glow
        for (int y = 0; y < CELL_SIZE; y++)
        {
            for (int x = 0; x < CELL_SIZE; x++)
            {
                int draw_x = px + x;
                int draw_y = py + y;
                // Bolt shape: zig-zag
                bool fill = false;
                if (y > 2 && y < CELL_SIZE - 2)
                {
                    int relx = x - CELL_SIZE / 2;
                    int rely = y - 2;
                    if ((rely > 0 && rely < CELL_SIZE / 2 && relx > -2 && relx < 3 && relx > (rely / 3) - 2) || (rely >= CELL_SIZE / 2 && relx > 0 && relx < 5 && relx < (rely / 2) + 2))
                        fill = true;
                }
                if (fill)
                {
                    float t = 0.8f + 0.2f * (float)y / (float)CELL_SIZE;
                    uint8_t r = (SPEED_COLOUR >> 16) & 0xFF;
                    uint8_t g = (SPEED_COLOUR >> 8) & 0xFF;
                    uint8_t b = SPEED_COLOUR & 0xFF;
                    r = (uint8_t)(r * t);
                    g = (uint8_t)(g * t);
                    b = (uint8_t)(b * t);
                    colour_t col = (r << 16) | (g << 8) | b;
                    // Highlight
                    if (x < CELL_SIZE / 2)
                        col = lerp_colour(col, RGB(255, 255, 180), 0.18f);
                    // Central shine
                    if (x == CELL_SIZE / 2 || y == CELL_SIZE / 2)
                        col = lerp_colour(col, RGB(255, 255, 255), 0.18f);
                    if (draw_x >= 0 && draw_x < FB_WIDTH && draw_y >= 0 && draw_y < FB_HEIGHT)
                        video_buffer[draw_y * FB_WIDTH + draw_x] = col;
                }
                // Glow
                else if (y > 1 && y < CELL_SIZE - 1 && x > 1 && x < CELL_SIZE - 1)
                {
                    if ((x + y) % 7 == 0)
                    {
                        colour_t col = lerp_colour(SPEED_COLOUR, RGB(255, 255, 180), 0.12f);
                        if (draw_x >= 0 && draw_x < FB_WIDTH && draw_y >= 0 && draw_y < FB_HEIGHT)
                            video_buffer[draw_y * FB_WIDTH + draw_x] = col;
                    }
                }
            }
        }
    }
}

/* Draw a seven‑segment digit at the specified pixel position. The
 * digit occupies a 20×36 pixel area. */
static void draw_segment(int x, int y, int px, int py, int pw, int ph, colour_t colour)
{
    for (int yy = 0; yy < ph; yy++)
    {
        for (int xx = 0; xx < pw; xx++)
        {
            int draw_x = x + px + xx;
            int draw_y = y + py + yy;
            if (draw_x >= 0 && draw_x < FB_WIDTH && draw_y >= 0 && draw_y < FB_HEIGHT)
            {
                video_buffer[draw_y * FB_WIDTH + draw_x] = colour;
            }
        }
    }
}

static void draw_digit(int x, int y, int value, colour_t colour)
{
    if (value < 0 || value > 9)
        return;
    uint8_t mask = seven_seg_digits[value];
    /* Segment dimensions relative to digit area. */
    int w = 20;
    int h = 36;
    int thickness = 4;
    /* a: top horizontal */
    if (mask & 0b1000000)
        draw_segment(x, y, thickness, 0, w - 2 * thickness, thickness, colour);
    /* b: upper right vertical */
    if (mask & 0b0100000)
        draw_segment(x, y, w - thickness, thickness, thickness, h / 2 - thickness, colour);
    /* c: lower right vertical */
    if (mask & 0b0010000)
        draw_segment(x, y, w - thickness, h / 2, thickness, h / 2 - thickness, colour);
    /* d: bottom horizontal */
    if (mask & 0b0001000)
        draw_segment(x, y, thickness, h - thickness, w - 2 * thickness, thickness, colour);
    /* e: lower left vertical */
    if (mask & 0b0000100)
        draw_segment(x, y, 0, h / 2, thickness, h / 2 - thickness, colour);
    /* f: upper left vertical */
    if (mask & 0b0000010)
        draw_segment(x, y, 0, thickness, thickness, h / 2 - thickness, colour);
    /* g: middle horizontal */
    if (mask & 0b0000001)
        draw_segment(x, y, thickness, h / 2 - thickness / 2, w - 2 * thickness, thickness, colour);
}

/* Draw a string using the 8×8 bitmap font. Each character occupies an
 * 8×8 block. The baseline is at y+8 (characters sit above this). */
static void draw_text(int x, int y, const char *text, colour_t colour)
{
    for (const char *p = text; *p; ++p)
    {
        if (*p == ' ')
        {
            x += 8;
            continue;
        }
        const uint8_t *bm = get_glyph_bitmap(*p);
        if (bm)
        {
            for (int row = 0; row < 8; row++)
            {
                uint8_t bits = bm[row];
                for (int col = 0; col < 8; col++)
                {
                    if (bits & (1 << (7 - col)))
                    {
                        int px = x + col;
                        int py = y + row;
                        if (px >= 0 && px < FB_WIDTH && py >= 0 && py < FB_HEIGHT)
                        {
                            video_buffer[py * FB_WIDTH + px] = colour;
                        }
                    }
                }
            }
        }
        x += 8;
    }
}

/* Draw the scoreboard at the top of the screen. The current score
 * appears on the left and the high score on the right. Each number
 * is rendered using the seven‑segment display. */
static void draw_scoreboard(void)
{
    char label_score[] = "SCORE";
    char label_hi[] = "HI";
    int base_y = 16;
    int label_x = 8;
    draw_text(label_x, base_y, label_score, HUD_TEXT_COLOUR);
    /* Draw current score digits. We show up to 5 digits. */
    int sc = score;
    for (int i = 0; i < 5; i++)
    {
        int digit = sc % 10;
        sc /= 10;
        int dx = 8 + (4 - i) * 24;
        draw_digit(dx, 32, digit, HUD_TEXT_COLOUR);
    }
    /* Draw high score on the right. */
    draw_text(FB_WIDTH - 8 - 2 * 8 - 5 * 24 - 4, base_y, label_hi, HUD_TEXT_COLOUR);
    int hs = highscore;
    for (int i = 0; i < 5; i++)
    {
        int digit = hs % 10;
        hs /= 10;
        int dx = FB_WIDTH - 8 - (i + 1) * 24;
        draw_digit(dx, 32, digit, HUD_TEXT_COLOUR);
    }
    /* Draw power‑up icons on the HUD if active. */
    int icon_y = 8;
    int icon_x = FB_WIDTH / 2 - 32;
    if (phase_timer > 0)
    {
        /* Small diamond representing phasing in HUD */
        for (int y = 0; y < 12; y++)
        {
            for (int x = 0; x < 12; x++)
            {
                int dx = abs(x - 6);
                int dy = abs(y - 6);
                int draw_x = icon_x + x;
                int draw_y = icon_y + y;
                if (dx + dy < 6)
                {
                    if (draw_x >= 0 && draw_x < FB_WIDTH && draw_y >= 0 && draw_y < FB_HEIGHT)
                        video_buffer[draw_y * FB_WIDTH + draw_x] = PHASE_COLOUR;
                }
            }
        }
        icon_x += 16;
    }
    if (speed_timer > 0)
    {
        /* Small lightning bolt */
        for (int y = 0; y < 12; y++)
        {
            for (int x = 0; x < 12; x++)
            {
                bool fill = false;
                int draw_x = icon_x + x;
                int draw_y = icon_y + y;
                if (y < 4 && x > 6)
                    fill = true;
                else if (y >= 4 && y < 8 && x < 6)
                    fill = true;
                else if (y >= 8 && x > 6)
                    fill = true;
                if (fill)
                    if (draw_x >= 0 && draw_x < FB_WIDTH && draw_y >= 0 && draw_y < FB_HEIGHT)
                        video_buffer[draw_y * FB_WIDTH + draw_x] = SPEED_COLOUR;
            }
        }
    }
}

/* Draw the semi‑transparent game over overlay. Darkens the screen
 * slightly and prints a message in the centre. */
static void draw_gameover_overlay(void)
{
    /* Darken background by blending with black. */
    for (int y = 0; y < FB_HEIGHT; y++)
    {
        for (int x = 0; x < FB_WIDTH; x++)
        {
            colour_t c = video_buffer[y * FB_WIDTH + x];
            uint8_t r = (c >> 16) & 0xFF;
            uint8_t g = (c >> 8) & 0xFF;
            uint8_t b = c & 0xFF;
            r = (uint8_t)(r * 0.4f);
            g = (uint8_t)(g * 0.4f);
            b = (uint8_t)(b * 0.4f);
            video_buffer[y * FB_WIDTH + x] = (r << 16) | (g << 8) | b;
        }
    }
    /* Draw "GAME OVER" text centred. */
    const char *msg = "GAME OVER";
    int msg_len = (int)strlen(msg);
    int px = (FB_WIDTH - msg_len * 8) / 2;
    int py = FB_HEIGHT / 2 - 20;
    draw_text(px, py, msg, GAMEOVER_COLOUR);
    const char *ins = "PRESS START";
    int ins_len = (int)strlen(ins);
    px = (FB_WIDTH - ins_len * 8) / 2;
    py += 20;
    draw_text(px, py, ins, HUD_TEXT_COLOUR);
}

/* Clear the framebuffer with a vertical gradient background. */
static void clear_background(void)
{
    for (int y = 0; y < FB_HEIGHT; y++)
    {
        float t = (float)y / (float)FB_HEIGHT;
        colour_t c = lerp_colour(BG_COLOUR_TOP, BG_COLOUR_BOTTOM, t);
        for (int x = 0; x < FB_WIDTH; x++)
        {
            video_buffer[y * FB_WIDTH + x] = c;
        }
    }
}

/* Draw the entire frame: background, particles, snake, food, item,
 * HUD and overlays. */
static void draw_frame(void)
{
    clear_background();
    /* Draw particles first so objects draw on top. */
    for (int i = 0; i < MAX_PARTICLES; i++)
    {
        if (particles[i].active)
        {
            int px = (int)particles[i].x;
            int py = (int)particles[i].y;
            if (px >= 0 && px < FB_WIDTH && py >= 0 && py < FB_HEIGHT)
            {
                video_buffer[py * FB_WIDTH + px] = particles[i].colour;
            }
        }
    }
    draw_snake();
    draw_food();
    draw_item();
    draw_obstacles();
    draw_scoreboard();
    if (state == STATE_GAMEOVER)
    {
        draw_gameover_overlay();
    }
    else if (state == STATE_PAUSE)
    {
        /* Darken background and draw "PAUSED" */
        for (int y = 0; y < FB_HEIGHT; y++)
        {
            for (int x = 0; x < FB_WIDTH; x++)
            {
                colour_t c = video_buffer[y * FB_WIDTH + x];
                uint8_t r = (c >> 16) & 0xFF;
                uint8_t g = (c >> 8) & 0xFF;
                uint8_t b = c & 0xFF;
                r = (uint8_t)(r * 0.4f);
                g = (uint8_t)(g * 0.4f);
                b = (uint8_t)(b * 0.4f);
                video_buffer[y * FB_WIDTH + x] = (r << 16) | (g << 8) | b;
            }
        }
        const char *msg = "PAUSED";
        int len = (int)strlen(msg);
        int px = (FB_WIDTH - len * 8) / 2;
        int py = FB_HEIGHT / 2 - 4;
        draw_text(px, py, msg, HUD_TEXT_COLOUR);
    }
    else if (state == STATE_TITLE)
    {
        /* Title screen: show game name and instructions. */
        const char *title = "SNAKE";
        int len = (int)strlen(title);
        int px = (FB_WIDTH - len * 8) / 2;
        int py = FB_HEIGHT / 2 - 32;
        draw_text(px, py, title, HUD_TEXT_COLOUR);
        const char *sub = "PRESS START";
        len = (int)strlen(sub);
        px = (FB_WIDTH - len * 8) / 2;
        py += 24;
        draw_text(px, py, sub, HUD_TEXT_COLOUR);
        const char *inst = "ARROWS TO MOVE";
        len = (int)strlen(inst);
        px = (FB_WIDTH - len * 8) / 2;
        py += 16;
        draw_text(px, py, inst, HUD_TEXT_COLOUR);
    }
}

/* ------------------------------------------------------------------
 * Libretro core API implementation
 */

void retro_set_environment(retro_environment_t cb)
{
    env_cb = cb;
    /* Provide descriptive names for controls. */
    struct retro_input_descriptor desc[] = {
        {0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP, "Up"},
        {0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN, "Down"},
        {0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT, "Left"},
        {0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "Right"},
        {0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START, "Start"},
        {0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT, "Reset Highscore"},
        {0, 0, 0, 0, NULL}};
    env_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, desc);

    bool contentless = true;
    env_cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &contentless);
}

void retro_set_video_refresh(retro_video_refresh_t cb) { video_cb = cb; }
void retro_set_audio_sample(retro_audio_sample_t cb) { audio_cb = cb; }
void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { audio_batch_cb = cb; }
void retro_set_input_poll(retro_input_poll_t cb) { input_poll_cb = cb; }
void retro_set_input_state(retro_input_state_t cb) { input_state_cb = cb; }

void retro_init(void)
{
    /* Allocate framebuffer. */
    video_buffer = (colour_t *)malloc(FB_WIDTH * FB_HEIGHT * sizeof(uint32_t));
    video_pitch = FB_WIDTH * sizeof(uint32_t);
    /* Seed RNG. */
    srand((unsigned)time(NULL));
    /* Display a message via frontend environment (optional). */
    struct retro_message msg = {"Snake core loaded", 180};
    if (env_cb)
        env_cb(RETRO_ENVIRONMENT_SET_MESSAGE, &msg);
    state = STATE_TITLE;
    game_reset();
}

void retro_deinit(void)
{
    if (video_buffer)
    {
        free(video_buffer);
        video_buffer = NULL;
    }
}

unsigned retro_api_version(void)
{
    return RETRO_API_VERSION;
}

void retro_get_system_info(struct retro_system_info *info)
{
    info->library_name = "Snek Core";
    info->library_version = "1.0";
    info->valid_extensions = "";
    info->need_fullpath = false;
    info->block_extract = false;
}

void retro_get_system_av_info(struct retro_system_av_info *info)
{
    info->geometry.base_width = FB_WIDTH;
    info->geometry.base_height = FB_HEIGHT;
    info->geometry.max_width = FB_WIDTH;
    info->geometry.max_height = FB_HEIGHT;
    info->geometry.aspect_ratio = (float)FB_WIDTH / (float)FB_HEIGHT;
    info->timing.fps = 60.0;
    info->timing.sample_rate = 48000.0;
}

void retro_reset(void)
{
    game_reset();
}

/* Save state serialization. We simply store all relevant state in a
 * linear buffer. The format is not intended to be stable and may
 * change in future versions. */
size_t retro_serialize_size(void)
{
    return sizeof(int) * (2 * MAX_SNAKE_LENGTH + 5) + sizeof(game_state_t) + sizeof(unsigned long);
}

/* Required by libretro API: set controller type for a port. */
void retro_set_controller_port_device(unsigned port, unsigned device)
{
    (void)port;
    (void)device;
    /* No-op: this core only supports the standard joypad. */
}

bool retro_serialize(void *data, size_t size)
{
    size_t needed = retro_serialize_size();
    if (size < needed)
        return false;
    uint8_t *ptr = (uint8_t *)data;
    memcpy(ptr, snake_x, sizeof(int) * MAX_SNAKE_LENGTH);
    ptr += sizeof(int) * MAX_SNAKE_LENGTH;
    memcpy(ptr, snake_y, sizeof(int) * MAX_SNAKE_LENGTH);
    ptr += sizeof(int) * MAX_SNAKE_LENGTH;
    memcpy(ptr, &snake_length, sizeof(int));
    ptr += sizeof(int);
    memcpy(ptr, &snake_dir, sizeof(direction_t));
    ptr += sizeof(direction_t);
    memcpy(ptr, &food_x, sizeof(int));
    ptr += sizeof(int);
    memcpy(ptr, &food_y, sizeof(int));
    ptr += sizeof(int);
    memcpy(ptr, &item_type, sizeof(item_type_t));
    ptr += sizeof(item_type_t);
    memcpy(ptr, &item_x, sizeof(int));
    ptr += sizeof(int);
    memcpy(ptr, &item_y, sizeof(int));
    ptr += sizeof(int);
    memcpy(ptr, &phase_timer, sizeof(int));
    ptr += sizeof(int);
    memcpy(ptr, &speed_timer, sizeof(int));
    ptr += sizeof(int);
    memcpy(ptr, &score, sizeof(int));
    ptr += sizeof(int);
    memcpy(ptr, &highscore, sizeof(int));
    ptr += sizeof(int);
    memcpy(ptr, &state, sizeof(game_state_t));
    ptr += sizeof(game_state_t);
    memcpy(ptr, &move_counter, sizeof(int));
    ptr += sizeof(int);
    memcpy(ptr, &frame_count, sizeof(unsigned long));
    return true;
}

bool retro_unserialize(const void *data, size_t size)
{
    size_t needed = retro_serialize_size();
    if (size < needed)
        return false;
    const uint8_t *ptr = (const uint8_t *)data;
    memcpy(snake_x, ptr, sizeof(int) * MAX_SNAKE_LENGTH);
    ptr += sizeof(int) * MAX_SNAKE_LENGTH;
    memcpy(snake_y, ptr, sizeof(int) * MAX_SNAKE_LENGTH);
    ptr += sizeof(int) * MAX_SNAKE_LENGTH;
    memcpy(&snake_length, ptr, sizeof(int));
    ptr += sizeof(int);
    memcpy(&snake_dir, ptr, sizeof(direction_t));
    ptr += sizeof(direction_t);
    memcpy(&food_x, ptr, sizeof(int));
    ptr += sizeof(int);
    memcpy(&food_y, ptr, sizeof(int));
    ptr += sizeof(int);
    memcpy(&item_type, ptr, sizeof(item_type_t));
    ptr += sizeof(item_type_t);
    memcpy(&item_x, ptr, sizeof(int));
    ptr += sizeof(int);
    memcpy(&item_y, ptr, sizeof(int));
    ptr += sizeof(int);
    memcpy(&phase_timer, ptr, sizeof(int));
    ptr += sizeof(int);
    memcpy(&speed_timer, ptr, sizeof(int));
    ptr += sizeof(int);
    memcpy(&score, ptr, sizeof(int));
    ptr += sizeof(int);
    memcpy(&highscore, ptr, sizeof(int));
    ptr += sizeof(int);
    memcpy(&state, ptr, sizeof(game_state_t));
    ptr += sizeof(game_state_t);
    memcpy(&move_counter, ptr, sizeof(int));
    ptr += sizeof(int);
    memcpy(&frame_count, ptr, sizeof(unsigned long));
    return true;
}

void retro_cheat_reset(void)
{
    /* Not implemented */
}

void retro_cheat_set(unsigned index, bool enabled, const char *code)
{
    /* Not implemented */
}

/* Allow the core to start without content. */
bool retro_load_game(const struct retro_game_info *info)
{
    /* This core does not require external content. Accept NULL info. */
    (void)info;
    /* Request XRGB8888 pixel format. */
    /* Needs to happen here */
    unsigned fmt = RETRO_PIXEL_FORMAT_XRGB8888;
    env_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt);
    return true;
}

bool retro_load_game_special(unsigned game_type, const struct retro_game_info *info, size_t num_info)
{
    /* Accept special loading with no content. */
    (void)game_type;
    (void)info;
    (void)num_info;
    return true;
}

void retro_unload_game(void)
{
    /* Nothing to unload. */
}

unsigned retro_get_region(void)
{
    /* NTSC region by default. */
    return 0;
}

void *retro_get_memory_data(unsigned id)
{
    /* No persistent memory provided. */
    (void)id;
    return NULL;
}

size_t retro_get_memory_size(unsigned id)
{
    (void)id;
    return 0;
}

/* Execute one frame. Handles input, updates timers, moves the
 * snake at the configured speed, updates particles, draws the
 * framebuffer and outputs audio. */
void retro_run(void)
{
    handle_input();
    if (state == STATE_PLAY)
    {
        /* Update timers. */
        if (phase_timer > 0)
            phase_timer--;
        if (speed_timer > 0)
            speed_timer--;
        /* Determine movement interval based on speed boost. */
        int interval = BASE_MOVE_INTERVAL;
        if (speed_timer > 0)
            interval = BASE_MOVE_INTERVAL / 2;
        /* Countdown until the next step. */
        move_counter--;
        if (move_counter <= 0)
        {
            update_snake();
            move_counter = interval;
        }
        /* Update particles. */
        update_particles();
    }
    /* Draw everything. */
    draw_frame();
    /* Send video frame to frontend. */
    video_cb(video_buffer, FB_WIDTH, FB_HEIGHT, video_pitch);
    /* Generate silent audio. We output exactly 1 frame worth of samples per video frame. */
    /* For 48000 Hz sample rate and 60 fps: samples_per_frame = 48000 / 60 = 800 stereo frames (1600 samples) */
    static int16_t silence[1600];
    memset(silence, 0, sizeof(silence));
    audio_batch_cb(silence, 800); /* 800 stereo samples (1600 total samples) */
    frame_count++;
}