/*
* Game Configuration - Tunable gameplay constants
*
* This file contains all gameplay-related constants that designers
* and programmers might want to tweak. Engine constants are in their
* respective engine modules.
*/

#pragma once

/*============================================================================
* CHARACTER MOVEMENT
*============================================================================*/

/* Movement speed (world units per frame) */
#define PLAYER_MOVE_SPEED       30000

/* Turn/rotation speed (angle units per frame, 4096 = 360 degrees) */
#define PLAYER_TURN_SPEED       96

/*============================================================================
* CHARACTER ANIMATION
*============================================================================*/

/* Walk cycle animation speed (higher = faster leg/arm movement) */
#define WALK_CYCLE_SPEED        300

/* Arm swing amplitude (angle units, max rotation from neutral) */
#define ARM_SWING_ANGLE         200

/* Leg swing amplitude (angle units, max rotation from neutral) */
#define LEG_SWING_ANGLE         250

/* Speed at which limbs return to neutral when stopping */
#define LIMB_RETURN_SPEED       20

/* Body squash amount during walk (0-4096, where 4096 = 100% squash) */
#define BODY_SQUASH_AMOUNT      250

/* Idle breathing animation */
#define IDLE_BREATH_SPEED       30      /* How fast the breathing cycle runs */
#define IDLE_BREATH_AMOUNT      100      /* Body squash amount for breathing */
#define IDLE_HEAD_BOB           8       /* Subtle head movement amplitude */

/*============================================================================
* CAMERA
*============================================================================*/

/* Base distance from camera to character (lower = more zoomed in) */
#define CAMERA_DISTANCE         250

/* Camera follow smoothing (higher = slower/smoother, 1 = instant) */
#define CAMERA_FOLLOW_DIVISOR   16

/* Camera Y offset (vertical position relative to character, positive = above) */
#define CAMERA_Y_OFFSET        50

/*============================================================================
* INPUT
*============================================================================*/

/* Analog stick deadzone (0-127 range) */
#define ANALOG_DEADZONE         20

/*============================================================================
* WORLD OBJECTS
*============================================================================*/

/* House scale multiplier (2048 = 0.5x, 6144 = 1.5x, 4096 = 1.0x, 8192 = 2.0x, etc.) */
#define HOUSE_SCALE  3596

/*============================================================================
* FLOOR / TERRAIN
*============================================================================*/

/* Size of each floor tile in world units */
#define FLOOR_TILE_SIZE  256

/* Number of tiles in each direction from center */
#define FLOOR_GRID_SIZE  8

/* Y position of floor (below character) */
#define FLOOR_Y          80

/* Floor grass colors (hex: 0c5c67, 12916b, 0b2458) */
#define GRASS_COLOR_1_R  12
#define GRASS_COLOR_1_G  92
#define GRASS_COLOR_1_B  103

#define GRASS_COLOR_2_R  18
#define GRASS_COLOR_2_G  145
#define GRASS_COLOR_2_B  107

#define GRASS_COLOR_3_R  11
#define GRASS_COLOR_3_G  36
#define GRASS_COLOR_3_B  88

/*============================================================================
* VISUAL EFFECTS
*============================================================================*/

/* Background flash fade speed (per frame) */
#define BG_FLASH_FADE_SPEED     12

/* Background gradient colors (normal state) */
#define BG_TOP_R    60
#define BG_TOP_G    20
#define BG_TOP_B    90
#define BG_BOT_R    15
#define BG_BOT_G    5
#define BG_BOT_B    35

/* Background gradient colors (flash state) */
#define BG_FLASH_TOP_R  255
#define BG_FLASH_TOP_G  220
#define BG_FLASH_TOP_B  80
#define BG_FLASH_BOT_R  180
#define BG_FLASH_BOT_G  100
#define BG_FLASH_BOT_B  40

/*============================================================================
* AUDIO
*============================================================================*/

/* Sound effect playback sample rate */
#define SFX_SAMPLE_RATE         22050

/* Sound effect volume (0-0x3FFF) */
#define SFX_VOLUME              0x3FFF

/* Master volume (0-0x3FFF) */
#define MASTER_VOLUME           0x3FFF
