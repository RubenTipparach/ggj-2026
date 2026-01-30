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
#define PLAYER_MOVE_SPEED       5000

/* Turn/rotation speed (angle units per frame, 4096 = 360 degrees) */
#define PLAYER_TURN_SPEED       200

/*============================================================================
* CHARACTER ANIMATION
*============================================================================*/

/* Walk cycle animation speed (higher = faster leg/arm movement) */
#define WALK_CYCLE_SPEED        100

/* Arm swing amplitude (angle units, max rotation from neutral) */
#define ARM_SWING_ANGLE         200

/* Leg swing amplitude (angle units, max rotation from neutral) */
#define LEG_SWING_ANGLE         250

/* Speed at which limbs return to neutral when stopping */
#define LIMB_RETURN_SPEED       20

/*============================================================================
* CAMERA
*============================================================================*/

/* Base distance from camera to character (lower = more zoomed in) */
#define CAMERA_DISTANCE         180

/* Camera follow smoothing (higher = slower/smoother, 1 = instant) */
#define CAMERA_FOLLOW_DIVISOR   8

/* Camera Y offset (vertical position relative to character) */
#define CAMERA_Y_OFFSET         0

/*============================================================================
* INPUT
*============================================================================*/

/* Analog stick deadzone (0-127 range) */
#define ANALOG_DEADZONE         20

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
