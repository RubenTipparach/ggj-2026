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
	bool isCarrying;   /* True when carrying an item (arms raised, no swing) */
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

/* Update character animation based on turn and forward/backward input
 * turnInput: -1 = turn left, 0 = no turn, +1 = turn right
 * forwardInput: -1 = backward, 0 = no movement, +1 = forward
 * deltaTime: frame time in 8.8 fixed point (256 = 1.0 = normal frame)
 * walkCycleSpeed: animation speed (use WALK_CYCLE_SPEED or WALK_CYCLE_SPEED_INTERIOR) */
void updateCharacter(Character *chr, int16_t turnInput, int16_t forwardInput, int deltaTime, int walkCycleSpeed);

/* Draw character to DMA chain (uses camera's view matrix for proper 3D transform) */
void drawCharacter(DMAChain *chain, Character *chr, const Camera *cam);

/* Draw an item attached to the character (e.g., carried food box)
 * item: the model to draw
 * offsetY: base Y offset from body center
 * offsetZ: Z offset (forward from body)
 * bobAmount: how much to bob up/down with walk cycle
 * scale: model scale (4096 = 1.0x) */
void drawCharacterItem(DMAChain *chain, Character *chr, const Model *item, const Camera *cam,
	int16_t offsetY, int16_t offsetZ, int16_t bobAmount, int16_t scale);

/* Draw an item at a static world position (e.g., food box on table)
 * item: the model to draw
 * worldX, worldY, worldZ: world position (not fixed-point, regular ints)
 * rotation: Y rotation (0-4095 = 0-360 degrees)
 * scale: model scale (4096 = 1.0x) */
void drawWorldItem(DMAChain *chain, const Model *item, const Camera *cam,
	int32_t worldX, int32_t worldY, int32_t worldZ, int16_t rotation, int16_t scale);

/* Draw enforcer with animated legs
 * bodyModel: body + head model
 * legLeftModel, legRightModel: leg models with pivot at top (hip)
 * x, y, z: position in fixed-point 20.12 format
 * facing: direction (0-4095)
 * walkCycle: animation timer (0-4095)
 * isWalking: true if enforcer is moving */
void drawEnforcer(DMAChain *chain,
	const Model *bodyModel, const Model *legLeftModel, const Model *legRightModel,
	int32_t x, int32_t y, int32_t z, int16_t facing,
	int16_t walkCycle, bool isWalking,
	const Camera *cam);

#ifdef __cplusplus
}
#endif
