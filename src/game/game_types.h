/*
 * Game Types - Shared data structures for the game
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "model.h"

/* Maximum collision boxes per house (for concave shapes) */
#define MAX_COLLISION_BOXES 4

/* Axis-Aligned Bounding Box for collision (in local space, relative to house center) */
typedef struct {
	int32_t minX, minZ;  /* Min corner (world units) */
	int32_t maxX, maxZ;  /* Max corner (world units) */
} CollisionBox;

/* Door trigger zone (offset from house center, in local space before scaling) */
typedef struct {
	int32_t offsetX, offsetZ;  /* Offset from house center */
	int32_t sizeX, sizeZ;      /* Half-size of trigger zone */
} DoorTrigger;

/* House structure - static world object */
typedef struct {
	Model model;
	int32_t x, y, z;  /* World position */
	int16_t rotation; /* Y rotation (0-4095 = 0-360 degrees) */

	/* Collision data */
	int numCollisionBoxes;
	CollisionBox collisionBoxes[MAX_COLLISION_BOXES];

	/* Door trigger */
	DoorTrigger door;
} House;

/* Tree structure - static world object */
typedef struct {
	Model model;
	int32_t x, y, z;  /* World position */
} Tree;

/* Game state for scene transitions */
typedef enum {
	STATE_TITLE,         /* Title screen */
	STATE_INTRO_1,       /* First intro text (quote) */
	STATE_INTRO_2,       /* Second intro text (story) */
	STATE_DAY_INTRO,     /* Day intro screen (DAY X - Threat level Y) */
	STATE_EXTERIOR,      /* Normal outdoor gameplay */
	STATE_FADE_OUT,      /* Fading to black before transition */
	STATE_BLACK,         /* Holding on black while scene switches */
	STATE_FADE_IN,       /* Fading from black after transition */
	STATE_INTERIOR,      /* Inside a house */
	STATE_DIALOG,        /* Showing NPC dialog */
	STATE_PAUSED         /* Game paused, showing map */
} GameState;

/* Day intro display duration (delta time units: 256 = 1 frame at 60fps, 5 seconds) */
#define DAY_INTRO_DURATION (60 * 5 * 256)

/* Typewriter effect speed (characters per frame) */
#define INTRO_TEXT_SPEED 1

/* How many frames to hold on black before fade-in */
#define FADE_HOLD_FRAMES 10
