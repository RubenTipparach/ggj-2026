/*
 * Character system with body parts and walking animation for PS1 bare-metal
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "model.h"
#include "gpu.h"
#include "camera.h"

/* Character body part indices */
#define PART_BODY      0
#define PART_HEAD      1
#define PART_ARM_LEFT  2
#define PART_ARM_RIGHT 3
#define PART_LEG_LEFT  4
#define PART_LEG_RIGHT 5
#define NUM_BODY_PARTS 6

/* Character state */
typedef struct {
	/* World position (fixed-point 20.12) */
	int32_t x, y, z;

	/* Character facing direction (0-4095 = 0-360 degrees) */
	int16_t facing;
	int16_t targetFacing;  /* Target facing for smooth interpolation */

	/* Movement state */
	bool isWalking;
	int16_t walkCycle;  /* Animation timer (0-4095) */
	int16_t bodySquash; /* Squash/stretch for torso (0 = none, positive = squashed) */

	/* Body part models */
	Model parts[NUM_BODY_PARTS];

	/* Part offsets from body center (in model units) */
	int16_t partOffsetX[NUM_BODY_PARTS];
	int16_t partOffsetY[NUM_BODY_PARTS];
	int16_t partOffsetZ[NUM_BODY_PARTS];

	/* Part rotations (current animation state) */
	int16_t partRotX[NUM_BODY_PARTS];
	int16_t partRotY[NUM_BODY_PARTS];
	int16_t partRotZ[NUM_BODY_PARTS];
} Character;

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize character with body part models */
bool initCharacter(Character *chr,
	const uint8_t *bodyData, uint32_t bodySize,
	const uint8_t *headData, uint32_t headSize,
	const uint8_t *armLeftData, uint32_t armLeftSize,
	const uint8_t *armRightData, uint32_t armRightSize,
	const uint8_t *legLeftData, uint32_t legLeftSize,
	const uint8_t *legRightData, uint32_t legRightSize);

/* Update character animation based on movement */
void updateCharacter(Character *chr, int16_t moveX, int16_t moveZ);

/* Draw character to DMA chain (uses camera's view matrix for proper 3D transform) */
void drawCharacter(DMAChain *chain, Character *chr, const Camera *cam);

#ifdef __cplusplus
}
#endif
