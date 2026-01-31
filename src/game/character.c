/*
 * Character system with body parts and walking animation for PS1 bare-metal
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include "character.h"
#include "model.h"
#include "gpu.h"
#include "camera.h"
#include "transform.h"
#include "trig.h"
#include "game_config.h"
#include "ps1/gpucmd.h"
#include "ps1/gte.h"

/* GTE fixed-point format (20.12) */
#define ONE (1 << 12)

bool initCharacter(Character *chr,
	const uint8_t *bodyData, uint32_t bodySize,
	const uint8_t *headData, uint32_t headSize,
	const uint8_t *armLeftData, uint32_t armLeftSize,
	const uint8_t *armRightData, uint32_t armRightSize,
	const uint8_t *legLeftData, uint32_t legLeftSize,
	const uint8_t *legRightData, uint32_t legRightSize)
{
	/* Initialize position */
	chr->x = 0;
	chr->y = 0;
	chr->z = 0;
	chr->facing = 0;
	chr->targetFacing = 0;
	chr->isWalking = false;
	chr->walkCycle = 0;
	chr->bodySquash = 0;

	/* Load body parts (using character model loader with vertex colors) */
	if (!loadCharacterModel(&chr->parts[PART_BODY], bodyData, bodySize)) {
		puts("Failed to load body");
		return false;
	}

	if (!loadCharacterModel(&chr->parts[PART_HEAD], headData, headSize)) {
		puts("Failed to load head");
		return false;
	}

	if (!loadCharacterModel(&chr->parts[PART_ARM_LEFT], armLeftData, armLeftSize)) {
		puts("Failed to load left arm");
		return false;
	}

	if (!loadCharacterModel(&chr->parts[PART_ARM_RIGHT], armRightData, armRightSize)) {
		puts("Failed to load right arm");
		return false;
	}

	if (!loadCharacterModel(&chr->parts[PART_LEG_LEFT], legLeftData, legLeftSize)) {
		puts("Failed to load left leg");
		return false;
	}

	if (!loadCharacterModel(&chr->parts[PART_LEG_RIGHT], legRightData, legRightSize)) {
		puts("Failed to load right leg");
		return false;
	}

	/* Set up part offsets to position pivots at joint locations
	 * Pivot points from conversion tool (relative to body center at OBJ Y=4.22):
	 *   Head pivot (neck): Y=5.63 → offset Y = 1.41 → PS1 Y = -28 (up)
	 *   Arm pivots (shoulder): Y=6.58 → offset Y = 2.36 → PS1 Y = -47 (up)
	 *   Leg pivots (hip): Y=1.79 → offset Y = -2.43 → PS1 Y = 49 (down)
	 *
	 * PS1 coords: X=right, Y=down (negative=up), Z=into screen
	 */

	/* Body is at center */
	chr->partOffsetX[PART_BODY] = 0;
	chr->partOffsetY[PART_BODY] = 0;
	chr->partOffsetZ[PART_BODY] = 0;

	/* Head pivot at neck - position at top of torso */
	chr->partOffsetX[PART_HEAD] = 1;
	chr->partOffsetY[PART_HEAD] = -28;  /* Up to neck position */
	chr->partOffsetZ[PART_HEAD] = 9;

	/* Arm pivots at shoulders */
	chr->partOffsetX[PART_ARM_LEFT] = 25;   /* OBJ +X = right side */
	chr->partOffsetY[PART_ARM_LEFT] = -47;  /* Up to shoulder */
	chr->partOffsetZ[PART_ARM_LEFT] = 1;

	chr->partOffsetX[PART_ARM_RIGHT] = -25;  /* OBJ -X = left side */
	chr->partOffsetY[PART_ARM_RIGHT] = -47;
	chr->partOffsetZ[PART_ARM_RIGHT] = 1;

	/* Leg pivots at hips */
	chr->partOffsetX[PART_LEG_LEFT] = 14;   /* OBJ +X */
	chr->partOffsetY[PART_LEG_LEFT] = 49;   /* Down to hip */
	chr->partOffsetZ[PART_LEG_LEFT] = 1;

	chr->partOffsetX[PART_LEG_RIGHT] = -14;  /* OBJ -X */
	chr->partOffsetY[PART_LEG_RIGHT] = 49;
	chr->partOffsetZ[PART_LEG_RIGHT] = 1;

	/* Initialize rotations to zero */
	for (int i = 0; i < NUM_BODY_PARTS; i++) {
		chr->partRotX[i] = 0;
		chr->partRotY[i] = 0;
		chr->partRotZ[i] = 0;
	}

	printf("Character loaded: body=%d, head=%d, arms=%d/%d, legs=%d/%d faces\n",
		chr->parts[PART_BODY].numFaces,
		chr->parts[PART_HEAD].numFaces,
		chr->parts[PART_ARM_LEFT].numFaces,
		chr->parts[PART_ARM_RIGHT].numFaces,
		chr->parts[PART_LEG_LEFT].numFaces,
		chr->parts[PART_LEG_RIGHT].numFaces);

	return true;
}

void updateCharacter(Character *chr, int16_t turnInput, int16_t forwardInput) {
	/* Handle turning - directly modify facing */
	if (turnInput != 0) {
		chr->facing += turnInput * PLAYER_TURN_SPEED;

		/* Keep facing in valid range */
		while (chr->facing > 2048) chr->facing -= 4096;
		while (chr->facing < -2048) chr->facing += 4096;
	}

	/* Sync targetFacing with actual facing */
	chr->targetFacing = chr->facing;

	/* Handle forward/backward movement */
	if (forwardInput != 0) {
		chr->isWalking = true;

		/* Calculate movement direction from character's facing angle
		 * facing=0 means looking down +Z axis
		 * sin(facing) = X component, cos(facing) = Z component */
		int32_t sinFacing = isin(chr->facing);
		int32_t cosFacing = icos(chr->facing);

		/* Move in the direction character is facing
		 * forwardInput > 0 = forward (away from camera)
		 * forwardInput < 0 = backward (toward camera) */
		chr->x += (sinFacing * forwardInput * PLAYER_MOVE_SPEED) >> FP_SHIFT;
		chr->z += (cosFacing * forwardInput * PLAYER_MOVE_SPEED) >> FP_SHIFT;

		/* Advance walk cycle */
		chr->walkCycle += WALK_CYCLE_SPEED;
		if (chr->walkCycle >= 4096) {
			chr->walkCycle -= 4096;
		}
	} else {
		chr->isWalking = false;
	}

	/* Calculate limb rotations based on walk cycle */
	if (chr->isWalking) {
		/* Sine wave for smooth oscillation
		 * walkCycle and isin both use 4096 = full cycle */
		int swing = isin(chr->walkCycle);  /* Returns -ONE to +ONE */

		/* Arms swing opposite to legs */
		int armSwing = (swing * ARM_SWING_ANGLE) / ONE;
		int legSwing = (swing * LEG_SWING_ANGLE) / ONE;

		/* Left arm and right leg swing forward together */
		chr->partRotX[PART_ARM_LEFT] = armSwing;
		chr->partRotX[PART_ARM_RIGHT] = -armSwing;

		/* Legs swing opposite to arms */
		chr->partRotX[PART_LEG_LEFT] = -legSwing;
		chr->partRotX[PART_LEG_RIGHT] = legSwing;

		/* Body squash: use absolute value of sine for squash at foot contact */
		/* Double frequency for squash (squash twice per walk cycle) */
		int squashWave = isin(chr->walkCycle * 2);
		squashWave = (squashWave < 0) ? -squashWave : squashWave;  /* Abs value */
		chr->bodySquash = (squashWave * BODY_SQUASH_AMOUNT) / ONE;
	} else {
		/* Idle pose with breathing animation */

		/* Continue walk cycle at slower speed for breathing */
		chr->walkCycle += IDLE_BREATH_SPEED;
		if (chr->walkCycle >= 4096) {
			chr->walkCycle -= 4096;
		}

		/* Gentle breathing: body expands/contracts */
		int breathWave = isin(chr->walkCycle);  /* -ONE to +ONE */
		/* Convert to 0 to IDLE_BREATH_AMOUNT range (inhale = expand = negative squash) */
		chr->bodySquash = -(breathWave * IDLE_BREATH_AMOUNT) / ONE;

		/* Subtle head bob with breathing */
		chr->partRotX[PART_HEAD] = (breathWave * IDLE_HEAD_BOB) / ONE;

		/* Return limbs to neutral pose */
		for (int i = PART_ARM_LEFT; i <= PART_LEG_RIGHT; i++) {
			if (chr->partRotX[i] > 0) {
				chr->partRotX[i] -= LIMB_RETURN_SPEED;
				if (chr->partRotX[i] < 0) chr->partRotX[i] = 0;
			} else if (chr->partRotX[i] < 0) {
				chr->partRotX[i] += LIMB_RETURN_SPEED;
				if (chr->partRotX[i] > 0) chr->partRotX[i] = 0;
			}
		}
	}
}

/* Draw a single body part with vertex colors */
static void drawBodyPart(DMAChain *chain, const Model *model,
	int32_t offsetX, int32_t offsetY, int32_t offsetZ,
	int16_t localPitch, int16_t localYaw, int16_t localRoll,
	int32_t baseX, int32_t baseY, int32_t baseZ,
	int16_t parentYaw,
	int16_t scaleX, int16_t scaleY, int16_t scaleZ)  /* Scale in 4.12 fixed point (4096 = 1.0) */
{
	/*
	 * Hierarchical transform using the transform module:
	 *
	 * Parent transform: character body position + facing (yaw only)
	 * Child transform: limb offset + local rotation (pitch for arm/leg swing)
	 *
	 * Combined = Parent * Child
	 * This means: first rotate in local space, then in world space
	 */

	Transform parent, child, combined;

	/* Parent transform: character facing (yaw only) */
	transformIdentity(&parent);
	transformSetRotation(&parent, parentYaw, 0, 0);
	transformSetTranslation(&parent, baseX, baseY, baseZ);

	/* Child transform: limb local rotation + offset from body */
	transformIdentity(&child);
	transformSetRotation(&child, localYaw, localPitch, localRoll);
	transformSetTranslation(&child, offsetX, offsetY, offsetZ);

	/* Combine: result = parent * child */
	transformCombine(&combined, &parent, &child);

	/* Load into GTE */
	transformLoadToGTE(&combined);

	/* Draw all faces of this part */
	for (int i = 0; i < model->numFaces; i++) {
		const Face *face = &model->faces[i];

		/* Apply scale to vertices if not identity */
		GTEVector16 v0, v1, v2;
		if (scaleX != ONE || scaleY != ONE || scaleZ != ONE) {
			v0.x = (model->vertices[face->v0].x * scaleX) >> 12;
			v0.y = (model->vertices[face->v0].y * scaleY) >> 12;
			v0.z = (model->vertices[face->v0].z * scaleZ) >> 12;
			v0._padding = 0;

			v1.x = (model->vertices[face->v1].x * scaleX) >> 12;
			v1.y = (model->vertices[face->v1].y * scaleY) >> 12;
			v1.z = (model->vertices[face->v1].z * scaleZ) >> 12;
			v1._padding = 0;

			v2.x = (model->vertices[face->v2].x * scaleX) >> 12;
			v2.y = (model->vertices[face->v2].y * scaleY) >> 12;
			v2.z = (model->vertices[face->v2].z * scaleZ) >> 12;
			v2._padding = 0;
		} else {
			v0 = model->vertices[face->v0];
			v1 = model->vertices[face->v1];
			v2 = model->vertices[face->v2];
		}

		/* Load vertices into GTE */
		gte_loadV0(&v0);
		gte_loadV1(&v1);
		gte_loadV2(&v2);

		/* Perspective transformation */
		gte_command(GTE_CMD_RTPT | GTE_SF);

		/* Backface culling (flipped for correct winding order) */
		gte_command(GTE_CMD_NCLIP);
		int nclip = gte_getDataReg(GTE_MAC0);
		if (nclip >= 0) continue;

		/* Calculate average Z for depth sorting */
		gte_command(GTE_CMD_AVSZ3 | GTE_SF);
		int zIndex = gte_getDataReg(GTE_OTZ);

		if (zIndex < 0 || zIndex >= ORDERING_TABLE_SIZE)
			continue;

		/* Get vertex colors */
		uint8_t r0, g0, b0, r1, g1, b1, r2, g2, b2;
		if (model->colors) {
			r0 = model->colors[face->v0].r;
			g0 = model->colors[face->v0].g;
			b0 = model->colors[face->v0].b;
			r1 = model->colors[face->v1].r;
			g1 = model->colors[face->v1].g;
			b1 = model->colors[face->v1].b;
			r2 = model->colors[face->v2].r;
			g2 = model->colors[face->v2].g;
			b2 = model->colors[face->v2].b;
		} else {
			/* Fallback to default color */
			r0 = g0 = b0 = r1 = g1 = b1 = r2 = g2 = b2 = 128;
		}

		/* Allocate packet for Gouraud-shaded triangle (6 words) */
		uint32_t *ptr = allocatePacket(chain, zIndex, 6);
		ptr[0] = gp0_rgb(r0, g0, b0) | gp0_shadedTriangle(true, false, false);
		gte_storeDataReg(GTE_SXY0, 1 * 4, ptr);  /* XY0 */
		ptr[2] = gp0_rgb(r1, g1, b1);
		gte_storeDataReg(GTE_SXY1, 3 * 4, ptr);  /* XY1 */
		ptr[4] = gp0_rgb(r2, g2, b2);
		gte_storeDataReg(GTE_SXY2, 5 * 4, ptr);  /* XY2 */
	}
}

void drawCharacter(DMAChain *chain, Character *chr, const Camera *cam)
{
	/* Calculate character position relative to camera (in world space) */
	int32_t relX = (chr->x >> 12) - cam->x;
	int32_t relY = (chr->y >> 12) - cam->y;
	int32_t relZ = (chr->z >> 12) - cam->z;

	/* Apply camera's full view rotation matrix (includes both pitch and yaw) */
	int32_t viewX = ((int32_t)cam->viewRotation.m[0][0] * relX +
	                 (int32_t)cam->viewRotation.m[0][1] * relY +
	                 (int32_t)cam->viewRotation.m[0][2] * relZ) >> FP_SHIFT;
	int32_t viewY = ((int32_t)cam->viewRotation.m[1][0] * relX +
	                 (int32_t)cam->viewRotation.m[1][1] * relY +
	                 (int32_t)cam->viewRotation.m[1][2] * relZ) >> FP_SHIFT;
	int32_t viewZ = ((int32_t)cam->viewRotation.m[2][0] * relX +
	                 (int32_t)cam->viewRotation.m[2][1] * relY +
	                 (int32_t)cam->viewRotation.m[2][2] * relZ) >> FP_SHIFT;

	/* Character facing adjusted for camera rotation */
	int16_t adjustedFacing = chr->facing - cam->yaw;

	/* Calculate body squash scale (squash Y, stretch XZ to preserve volume) */
	int16_t bodyScaleY = ONE - chr->bodySquash;  /* Squash in Y */
	int16_t bodyScaleXZ = ONE + (chr->bodySquash / 2);  /* Slight stretch in X and Z */

	/* Calculate Y offset compensation for head and arms (they need to move down when body squashes) */
	/* The offset change is proportional to how much the body height changed */
	int16_t yCompensation = (chr->partOffsetY[PART_HEAD] * chr->bodySquash) / ONE;

	/* Draw each body part */
	/* Draw body first (it's the base) with squash */
	drawBodyPart(chain, &chr->parts[PART_BODY],
		chr->partOffsetX[PART_BODY],
		chr->partOffsetY[PART_BODY],
		chr->partOffsetZ[PART_BODY],
		chr->partRotX[PART_BODY],
		chr->partRotY[PART_BODY],
		chr->partRotZ[PART_BODY],
		viewX, viewY, viewZ, adjustedFacing,
		bodyScaleXZ, bodyScaleY, bodyScaleXZ);

	/* Draw head (no scale, but compensate offset for body squash) */
	drawBodyPart(chain, &chr->parts[PART_HEAD],
		chr->partOffsetX[PART_HEAD],
		chr->partOffsetY[PART_HEAD] + yCompensation,
		chr->partOffsetZ[PART_HEAD],
		chr->partRotX[PART_HEAD],
		chr->partRotY[PART_HEAD],
		chr->partRotZ[PART_HEAD],
		viewX, viewY, viewZ, adjustedFacing,
		ONE, ONE, ONE);

	/* Draw arms (no scale, but compensate offset for body squash) */
	drawBodyPart(chain, &chr->parts[PART_ARM_LEFT],
		chr->partOffsetX[PART_ARM_LEFT],
		chr->partOffsetY[PART_ARM_LEFT] + yCompensation,
		chr->partOffsetZ[PART_ARM_LEFT],
		chr->partRotX[PART_ARM_LEFT],
		chr->partRotY[PART_ARM_LEFT],
		chr->partRotZ[PART_ARM_LEFT],
		viewX, viewY, viewZ, adjustedFacing,
		ONE, ONE, ONE);

	drawBodyPart(chain, &chr->parts[PART_ARM_RIGHT],
		chr->partOffsetX[PART_ARM_RIGHT],
		chr->partOffsetY[PART_ARM_RIGHT] + yCompensation,
		chr->partOffsetZ[PART_ARM_RIGHT],
		chr->partRotX[PART_ARM_RIGHT],
		chr->partRotY[PART_ARM_RIGHT],
		chr->partRotZ[PART_ARM_RIGHT],
		viewX, viewY, viewZ, adjustedFacing,
		ONE, ONE, ONE);

	/* Draw legs (no scale, legs attach below body so no compensation needed) */
	drawBodyPart(chain, &chr->parts[PART_LEG_LEFT],
		chr->partOffsetX[PART_LEG_LEFT],
		chr->partOffsetY[PART_LEG_LEFT],
		chr->partOffsetZ[PART_LEG_LEFT],
		chr->partRotX[PART_LEG_LEFT],
		chr->partRotY[PART_LEG_LEFT],
		chr->partRotZ[PART_LEG_LEFT],
		viewX, viewY, viewZ, adjustedFacing,
		ONE, ONE, ONE);

	drawBodyPart(chain, &chr->parts[PART_LEG_RIGHT],
		chr->partOffsetX[PART_LEG_RIGHT],
		chr->partOffsetY[PART_LEG_RIGHT],
		chr->partOffsetZ[PART_LEG_RIGHT],
		chr->partRotX[PART_LEG_RIGHT],
		chr->partRotY[PART_LEG_RIGHT],
		chr->partRotZ[PART_LEG_RIGHT],
		viewX, viewY, viewZ, adjustedFacing,
		ONE, ONE, ONE);
}
