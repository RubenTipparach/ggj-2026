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
#include "kids_female_L_offsets.h"
#include "ps1/gpucmd.h"
#include "ps1/gte.h"

/* GTE fixed-point format (20.12) */
#define ONE (1 << 12)

/* Distance fog helper functions */
static inline uint8_t applyFog(int32_t distance, uint8_t color, uint8_t fogColor) {
	if (distance <= FOG_NEAR_DISTANCE) return color;
	if (distance >= FOG_FAR_DISTANCE) return fogColor;

	int32_t fogRange = FOG_FAR_DISTANCE - FOG_NEAR_DISTANCE;
	int32_t fogDist = distance - FOG_NEAR_DISTANCE;
	int32_t fogFactor = (fogDist * 256) / fogRange;

	return (uint8_t)(((256 - fogFactor) * color + fogFactor * fogColor) >> 8);
}

static inline void applyFogRGB(int32_t distance, uint8_t *r, uint8_t *g, uint8_t *b) {
	*r = applyFog(distance, *r, FOG_COLOR_R);
	*g = applyFog(distance, *g, FOG_COLOR_G);
	*b = applyFog(distance, *b, FOG_COLOR_B);
}

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
	chr->isCarrying = false;
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

	/* Part offsets from auto-generated header */
	chr->partOffsetX[PART_BODY] = KIDS_FEMALE_L_BODY_OFFSET_X;
	chr->partOffsetY[PART_BODY] = KIDS_FEMALE_L_BODY_OFFSET_Y;
	chr->partOffsetZ[PART_BODY] = KIDS_FEMALE_L_BODY_OFFSET_Z;

	chr->partOffsetX[PART_HEAD] = KIDS_FEMALE_L_HEAD_OFFSET_X;
	chr->partOffsetY[PART_HEAD] = KIDS_FEMALE_L_HEAD_OFFSET_Y;
	chr->partOffsetZ[PART_HEAD] = KIDS_FEMALE_L_HEAD_OFFSET_Z;

	chr->partOffsetX[PART_ARM_LEFT] = KIDS_FEMALE_L_ARM_LEFT_OFFSET_X;
	chr->partOffsetY[PART_ARM_LEFT] = KIDS_FEMALE_L_ARM_LEFT_OFFSET_Y;
	chr->partOffsetZ[PART_ARM_LEFT] = KIDS_FEMALE_L_ARM_LEFT_OFFSET_Z;

	chr->partOffsetX[PART_ARM_RIGHT] = KIDS_FEMALE_L_ARM_RIGHT_OFFSET_X;
	chr->partOffsetY[PART_ARM_RIGHT] = KIDS_FEMALE_L_ARM_RIGHT_OFFSET_Y;
	chr->partOffsetZ[PART_ARM_RIGHT] = KIDS_FEMALE_L_ARM_RIGHT_OFFSET_Z;

	chr->partOffsetX[PART_LEG_LEFT] = KIDS_FEMALE_L_LEG_LEFT_OFFSET_X;
	chr->partOffsetY[PART_LEG_LEFT] = KIDS_FEMALE_L_LEG_LEFT_OFFSET_Y;
	chr->partOffsetZ[PART_LEG_LEFT] = KIDS_FEMALE_L_LEG_LEFT_OFFSET_Z;

	chr->partOffsetX[PART_LEG_RIGHT] = KIDS_FEMALE_L_LEG_RIGHT_OFFSET_X;
	chr->partOffsetY[PART_LEG_RIGHT] = KIDS_FEMALE_L_LEG_RIGHT_OFFSET_Y;
	chr->partOffsetZ[PART_LEG_RIGHT] = KIDS_FEMALE_L_LEG_RIGHT_OFFSET_Z;

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

void updateCharacter(Character *chr, int16_t turnInput, int16_t forwardInput, int deltaTime, int walkCycleSpeed) {
	/* Handle turning - directly modify facing (scaled by deltaTime) */
	if (turnInput != 0) {
		chr->facing += (turnInput * PLAYER_TURN_SPEED * deltaTime) >> 8;

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

		/* Move in the direction character is facing (scaled by deltaTime)
		 * forwardInput > 0 = forward (away from camera)
		 * forwardInput < 0 = backward (toward camera)
		 * Calculate base movement first, then scale by deltaTime to avoid overflow */
		int32_t baseMovement = (sinFacing * forwardInput * PLAYER_MOVE_SPEED) >> FP_SHIFT;
		chr->x += (baseMovement * deltaTime) >> 8;
		baseMovement = (cosFacing * forwardInput * PLAYER_MOVE_SPEED) >> FP_SHIFT;
		chr->z += (baseMovement * deltaTime) >> 8;

		/* Advance walk cycle (scaled by deltaTime) */
		chr->walkCycle += (walkCycleSpeed * deltaTime) >> 8;
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

		/* Legs always swing during walk */
		int legSwing = (swing * LEG_SWING_ANGLE) / ONE;
		chr->partRotX[PART_LEG_LEFT] = -legSwing;
		chr->partRotX[PART_LEG_RIGHT] = legSwing;

		/* Arms: raised with bob when carrying, swinging when not */
		if (chr->isCarrying) {
			/* Arms raised forward with bobbing motion */
			int armBob = (swing * CARRY_ARM_BOB_AMOUNT) / ONE;
			chr->partRotX[PART_ARM_LEFT] = CARRY_ARM_ANGLE + armBob;
			chr->partRotX[PART_ARM_RIGHT] = CARRY_ARM_ANGLE + armBob;
		} else {
			/* Normal arm swing opposite to legs */
			int armSwing = (swing * ARM_SWING_ANGLE) / ONE;
			chr->partRotX[PART_ARM_LEFT] = armSwing;
			chr->partRotX[PART_ARM_RIGHT] = -armSwing;
		}

		/* Body squash: use absolute value of sine for squash at foot contact */
		/* Double frequency for squash (squash twice per walk cycle) */
		int squashWave = isin(chr->walkCycle * 2);
		squashWave = (squashWave < 0) ? -squashWave : squashWave;  /* Abs value */
		chr->bodySquash = (squashWave * BODY_SQUASH_AMOUNT) / ONE;

		/* Head bob: nod forward slightly with each step */
		chr->partRotX[PART_HEAD] = (squashWave * WALK_HEAD_BOB) / ONE;
	} else {
		/* Idle pose with breathing animation */

		/* Continue walk cycle at slower speed for breathing (scaled by deltaTime) */
		chr->walkCycle += (IDLE_BREATH_SPEED * deltaTime) >> 8;
		if (chr->walkCycle >= 4096) {
			chr->walkCycle -= 4096;
		}

		/* Gentle breathing: body expands/contracts */
		int breathWave = isin(chr->walkCycle);  /* -ONE to +ONE */
		/* Convert to 0 to IDLE_BREATH_AMOUNT range (inhale = expand = negative squash) */
		chr->bodySquash = -(breathWave * IDLE_BREATH_AMOUNT) / ONE;

		/* Subtle head bob with breathing */
		chr->partRotX[PART_HEAD] = (breathWave * IDLE_HEAD_BOB) / ONE;

		/* Arm handling when idle */
		if (chr->isCarrying) {
			/* Keep arms raised with gentle bob when carrying */
			int armBob = (breathWave * CARRY_ARM_BOB_AMOUNT) / ONE;
			chr->partRotX[PART_ARM_LEFT] = CARRY_ARM_ANGLE + armBob;
			chr->partRotX[PART_ARM_RIGHT] = CARRY_ARM_ANGLE + armBob;
			/* Return only legs to neutral */
			int returnSpeed = (LIMB_RETURN_SPEED * deltaTime) >> 8;
			for (int i = PART_LEG_LEFT; i <= PART_LEG_RIGHT; i++) {
				if (chr->partRotX[i] > 0) {
					chr->partRotX[i] -= returnSpeed;
					if (chr->partRotX[i] < 0) chr->partRotX[i] = 0;
				} else if (chr->partRotX[i] < 0) {
					chr->partRotX[i] += returnSpeed;
					if (chr->partRotX[i] > 0) chr->partRotX[i] = 0;
				}
			}
		} else {
			/* Return all limbs to neutral pose (scaled by deltaTime) */
			int returnSpeed = (LIMB_RETURN_SPEED * deltaTime) >> 8;
			for (int i = PART_ARM_LEFT; i <= PART_LEG_RIGHT; i++) {
				if (chr->partRotX[i] > 0) {
					chr->partRotX[i] -= returnSpeed;
					if (chr->partRotX[i] < 0) chr->partRotX[i] = 0;
				} else if (chr->partRotX[i] < 0) {
					chr->partRotX[i] += returnSpeed;
					if (chr->partRotX[i] > 0) chr->partRotX[i] = 0;
				}
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
	int16_t scaleX, int16_t scaleY, int16_t scaleZ,  /* Scale in 4.12 fixed point (4096 = 1.0) */
	const Camera *cam,  /* Camera for view rotation */
	int32_t viewDistance)  /* Distance from camera for fog calculation */
{
	/*
	 * Hierarchical transform using the transform module:
	 *
	 * Parent transform: character body position + facing (yaw only)
	 * Child transform: limb offset + local rotation (pitch for arm/leg swing)
	 *
	 * Combined = CameraView * Parent * Child
	 * This means: first rotate in local space, then character space, then view space
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

	/* Combine character transforms: charLocal = parent * child */
	transformCombine(&combined, &parent, &child);

	/* Apply camera pitch to rotation only (not translation) to keep character grounded */
	int32_t cp = icos(-cam->pitch);
	int32_t sp = isin(-cam->pitch);

	/* Multiply combined rotation by pitch rotation matrix (RotX) */
	/* RotX = [1, 0, 0; 0, cos, -sin; 0, sin, cos] */
	Matrix3x3 pitched;
	for (int col = 0; col < 3; col++) {
		pitched.m[0][col] = combined.rotation.m[0][col];
		pitched.m[1][col] = (cp * combined.rotation.m[1][col] - sp * combined.rotation.m[2][col]) >> FP_SHIFT;
		pitched.m[2][col] = (sp * combined.rotation.m[1][col] + cp * combined.rotation.m[2][col]) >> FP_SHIFT;
	}
	combined.rotation = pitched;

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

		/* Apply distance fog to vertex colors */
		applyFogRGB(viewDistance, &r0, &g0, &b0);
		applyFogRGB(viewDistance, &r1, &g1, &b1);
		applyFogRGB(viewDistance, &r2, &g2, &b2);

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
		bodyScaleXZ, bodyScaleY, bodyScaleXZ, cam, viewZ);

	/* Draw head (no scale, but compensate offset for body squash) */
	drawBodyPart(chain, &chr->parts[PART_HEAD],
		chr->partOffsetX[PART_HEAD],
		chr->partOffsetY[PART_HEAD] + yCompensation,
		chr->partOffsetZ[PART_HEAD],
		chr->partRotX[PART_HEAD],
		chr->partRotY[PART_HEAD],
		chr->partRotZ[PART_HEAD],
		viewX, viewY, viewZ, adjustedFacing,
		ONE, ONE, ONE, cam, viewZ);

	/* Draw arms (no scale, but compensate offset for body squash) */
	drawBodyPart(chain, &chr->parts[PART_ARM_LEFT],
		chr->partOffsetX[PART_ARM_LEFT],
		chr->partOffsetY[PART_ARM_LEFT] + yCompensation,
		chr->partOffsetZ[PART_ARM_LEFT],
		chr->partRotX[PART_ARM_LEFT],
		chr->partRotY[PART_ARM_LEFT],
		chr->partRotZ[PART_ARM_LEFT],
		viewX, viewY, viewZ, adjustedFacing,
		ONE, ONE, ONE, cam, viewZ);

	drawBodyPart(chain, &chr->parts[PART_ARM_RIGHT],
		chr->partOffsetX[PART_ARM_RIGHT],
		chr->partOffsetY[PART_ARM_RIGHT] + yCompensation,
		chr->partOffsetZ[PART_ARM_RIGHT],
		chr->partRotX[PART_ARM_RIGHT],
		chr->partRotY[PART_ARM_RIGHT],
		chr->partRotZ[PART_ARM_RIGHT],
		viewX, viewY, viewZ, adjustedFacing,
		ONE, ONE, ONE, cam, viewZ);

	/* Draw legs (no scale, legs attach below body so no compensation needed) */
	drawBodyPart(chain, &chr->parts[PART_LEG_LEFT],
		chr->partOffsetX[PART_LEG_LEFT],
		chr->partOffsetY[PART_LEG_LEFT],
		chr->partOffsetZ[PART_LEG_LEFT],
		chr->partRotX[PART_LEG_LEFT],
		chr->partRotY[PART_LEG_LEFT],
		chr->partRotZ[PART_LEG_LEFT],
		viewX, viewY, viewZ, adjustedFacing,
		ONE, ONE, ONE, cam, viewZ);

	drawBodyPart(chain, &chr->parts[PART_LEG_RIGHT],
		chr->partOffsetX[PART_LEG_RIGHT],
		chr->partOffsetY[PART_LEG_RIGHT],
		chr->partOffsetZ[PART_LEG_RIGHT],
		chr->partRotX[PART_LEG_RIGHT],
		chr->partRotY[PART_LEG_RIGHT],
		chr->partRotZ[PART_LEG_RIGHT],
		viewX, viewY, viewZ, adjustedFacing,
		ONE, ONE, ONE, cam, viewZ);
}

void drawCharacterItem(DMAChain *chain, Character *chr, const Model *item, const Camera *cam,
	int16_t offsetY, int16_t offsetZ, int16_t bobAmount, int16_t scale)
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

	/* Calculate bobbing offset based on walk cycle (sync with arm swing) */
	int16_t bobOffset = 0;
	if (chr->isWalking && bobAmount != 0) {
		/* Use same frequency as arm swing for synced motion */
		int bobWave = isin(chr->walkCycle);
		bobWave = (bobWave < 0) ? -bobWave : bobWave;  /* Abs value */
		bobOffset = (bobWave * bobAmount) / ONE;
	}

	/* Draw item using drawBodyPart with no local rotation */
	drawBodyPart(chain, item,
		0,                              /* No X offset (centered on character) */
		offsetY + bobOffset,            /* Y offset with bobbing */
		offsetZ,                        /* Z offset (forward) */
		0, 0, 0,                        /* No local rotation */
		viewX, viewY, viewZ, adjustedFacing,
		scale, scale, scale, cam, viewZ);
}

void drawWorldItem(DMAChain *chain, const Model *item, const Camera *cam,
	int32_t worldX, int32_t worldY, int32_t worldZ, int16_t rotation, int16_t scale)
{
	/* Calculate position relative to camera (in world space) */
	int32_t relX = worldX - cam->x;
	int32_t relY = worldY - cam->y;
	int32_t relZ = worldZ - cam->z;

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

	/* Rotation adjusted for camera yaw */
	int16_t adjustedRotation = rotation - cam->yaw;

	/* Draw item using drawBodyPart with no offsets */
	drawBodyPart(chain, item,
		0, 0, 0,                        /* No offset from position */
		0, 0, 0,                        /* No local rotation */
		viewX, viewY, viewZ, adjustedRotation,
		scale, scale, scale, cam, viewZ);
}

void drawEnforcer(DMAChain *chain,
	const Model *bodyModel, const Model *legLeftModel, const Model *legRightModel,
	int32_t x, int32_t y, int32_t z, int16_t facing,
	int16_t walkCycle, bool isWalking,
	const Camera *cam)
{
	/* Calculate enforcer position relative to camera (in world space) */
	int32_t relX = (x >> 12) - cam->x;
	int32_t relY = (y >> 12) - cam->y;
	int32_t relZ = (z >> 12) - cam->z;

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

	/* Facing adjusted for camera rotation */
	int16_t adjustedFacing = facing - cam->yaw;

	/* Calculate walk animation */
	int16_t yBob = 0;
	int16_t bodyLean = 0;
	int16_t legSwingLeft = 0;
	int16_t legSwingRight = 0;

	if (isWalking) {
		/* Use walkCycle (0-4095) for leg swing animation */
		int swing = isin(walkCycle);  /* Returns -ONE to +ONE */

		/* Leg swing - opposite directions like player character */
		int legSwing = (swing * LEG_SWING_ANGLE) / ONE;
		legSwingLeft = -legSwing;
		legSwingRight = legSwing;

		/* Double the frequency for body bob (2 bobs per full walk cycle) */
		int bobPhase = (walkCycle * 2) & 0xFFF;
		int bobWave = isin(bobPhase);
		yBob = (bobWave * 3) / ONE;    /* Small vertical bob */

		/* Add slight forward/back lean */
		bodyLean = (bobWave * 30) / ONE;
	}

	/* Draw body + head with Y offset and walk bob */
	drawBodyPart(chain, bodyModel,
		0, ENFORCER_Y_OFFSET + yBob, 0,
		bodyLean, 0, 0,
		viewX, viewY, viewZ, adjustedFacing,
		ENFORCER_SCALE, ENFORCER_SCALE, ENFORCER_SCALE, cam, viewZ);

	/* Draw left leg with swing animation
	 * Leg offset: positioned at hip (slightly below and to the side of body center) */
	drawBodyPart(chain, legLeftModel,
		ENFORCER_LEG_OFFSET_X, ENFORCER_Y_OFFSET + ENFORCER_LEG_OFFSET_Y, 0,
		legSwingLeft, 0, 0,  /* X rotation for forward/back swing */
		viewX, viewY, viewZ, adjustedFacing,
		ENFORCER_SCALE, ENFORCER_SCALE, ENFORCER_SCALE, cam, viewZ);

	/* Draw right leg with opposite swing animation */
	drawBodyPart(chain, legRightModel,
		-ENFORCER_LEG_OFFSET_X, ENFORCER_Y_OFFSET + ENFORCER_LEG_OFFSET_Y, 0,
		legSwingRight, 0, 0,
		viewX, viewY, viewZ, adjustedFacing,
		ENFORCER_SCALE, ENFORCER_SCALE, ENFORCER_SCALE, cam, viewZ);
}
