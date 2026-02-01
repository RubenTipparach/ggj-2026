/*
* PSX Character Demo - Bare Metal Version
* Walking character with camera following
*/

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "gpu.h"
#include "spu.h"
#include "cdda.h"
#include "bios.h"
#include "model.h"
#include "character.h"
#include "font.h"
#include "ps1/cop0.h"
#include "ps1/gpucmd.h"
#include "ps1/gte.h"
#include "ps1/registers.h"
#include "trig.h"
#include "transform.h"
#include "camera.h"
#include "game_config.h"
#include "world_data.h"

/* Simple hash-based 2D noise for tile variation (returns 0-255) */
static int tileNoise(int x, int z) {
	/* Hash function - mix coordinates to get pseudo-random value */
	int n = x * 374761393 + z * 668265263;
	n = (n ^ (n >> 13)) * 1274126177;
	return (n ^ (n >> 16)) & 255;
}

/* Performance stats */
static int statTriangles = 0;    /* Triangles rendered this frame */
static int statTiles = 0;        /* Floor tiles rendered this frame */
static int32_t statFrameTime = 0;    /* Frame time in timer ticks */
static int32_t statGpuWait = 0;      /* GPU wait time in timer ticks */
static int32_t statFloorTime = 0;    /* Floor drawing time */
static int32_t statCharTime = 0;     /* Character drawing time */
static int32_t statPadTime = 0;      /* Controller polling time */

/* Character body part data embedded by CMake */
extern const uint8_t charBodyData[];
extern const uint32_t charBodyData_size;
extern const uint8_t charHeadData[];
extern const uint32_t charHeadData_size;
extern const uint8_t charArmLeftData[];
extern const uint32_t charArmLeftData_size;
extern const uint8_t charArmRightData[];
extern const uint32_t charArmRightData_size;
extern const uint8_t charLegLeftData[];
extern const uint32_t charLegLeftData_size;
extern const uint8_t charLegRightData[];
extern const uint32_t charLegRightData_size;

/* House model data embedded by CMake */
extern const uint8_t house1Data[];
extern const uint32_t house1Data_size;
extern const uint8_t house2Data[];
extern const uint32_t house2Data_size;
extern const uint8_t house3Data[];
extern const uint32_t house3Data_size;

/* House interior model data embedded by CMake */
extern const uint8_t house1IntData[];
extern const uint32_t house1IntData_size;
extern const uint8_t house2IntData[];
extern const uint32_t house2IntData_size;
extern const uint8_t house3IntData[];
extern const uint32_t house3IntData_size;

/* Tree model data embedded by CMake */
extern const uint8_t treeLargeData[];
extern const uint32_t treeLargeData_size;
extern const uint8_t treeSmallData[];
extern const uint32_t treeSmallData_size;

/* Number of houses - use map data */
#define NUM_HOUSES NUM_MAP_HOUSES

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
	STATE_EXTERIOR,      /* Normal outdoor gameplay */
	STATE_FADE_OUT,      /* Fading to black before transition */
	STATE_BLACK,         /* Holding on black while scene switches */
	STATE_FADE_IN,       /* Fading from black after transition */
	STATE_INTERIOR       /* Inside a house */
} GameState;

/* How many frames to hold on black before fade-in */
#define FADE_HOLD_FRAMES 10

/* Check if a circle (player) collides with a single AABB
 * Returns true if collision detected */
static bool checkCircleBoxCollision(int32_t circleX, int32_t circleZ, int32_t radius,
                                     int32_t boxMinX, int32_t boxMinZ,
                                     int32_t boxMaxX, int32_t boxMaxZ) {
	/* Find closest point on box to circle center */
	int32_t closestX = circleX;
	int32_t closestZ = circleZ;

	if (circleX < boxMinX) closestX = boxMinX;
	else if (circleX > boxMaxX) closestX = boxMaxX;

	if (circleZ < boxMinZ) closestZ = boxMinZ;
	else if (circleZ > boxMaxZ) closestZ = boxMaxZ;

	/* Calculate distance from circle center to closest point */
	int32_t dx = circleX - closestX;
	int32_t dz = circleZ - closestZ;

	/* Use squared distance to avoid sqrt */
	int32_t distSq = dx * dx + dz * dz;
	int32_t radiusSq = radius * radius;

	return distSq < radiusSq;
}

/* Check if player collides with a house's collision boxes
 * House position is in world coordinates, boxes are in local space
 * Boxes are scaled by HOUSE_SCALE to match visual model */
static bool checkHouseCollision(int32_t playerX, int32_t playerZ, int32_t radius,
                                 const House *house) {
	/* Check each collision box */
	for (int i = 0; i < house->numCollisionBoxes; i++) {
		const CollisionBox *box = &house->collisionBoxes[i];

		/* Scale box by HOUSE_SCALE (4096 = 1.0x) to match model scaling */
		int32_t scaledMinX = (box->minX * HOUSE_SCALE) >> 12;
		int32_t scaledMinZ = (box->minZ * HOUSE_SCALE) >> 12;
		int32_t scaledMaxX = (box->maxX * HOUSE_SCALE) >> 12;
		int32_t scaledMaxZ = (box->maxZ * HOUSE_SCALE) >> 12;

		/* Transform box to world space (add house position) */
		int32_t worldMinX = house->x + scaledMinX;
		int32_t worldMinZ = house->z + scaledMinZ;
		int32_t worldMaxX = house->x + scaledMaxX;
		int32_t worldMaxZ = house->z + scaledMaxZ;

		if (checkCircleBoxCollision(playerX, playerZ, radius,
		                            worldMinX, worldMinZ,
		                            worldMaxX, worldMaxZ)) {
			return true;
		}
	}
	return false;
}

/* Check collision against all houses */
static bool checkAllHouseCollisions(int32_t playerX, int32_t playerZ, int32_t radius,
                                     const House *houses, int numHouses) {
	for (int i = 0; i < numHouses; i++) {
		if (checkHouseCollision(playerX, playerZ, radius, &houses[i])) {
			return true;
		}
	}
	return false;
}

/* Check if player is inside a house's door trigger zone
 * Returns index of triggered door (0-based), or -1 if no trigger */
static int checkDoorTrigger(int32_t playerX, int32_t playerZ,
                            const House *houses, int numHouses) {
	for (int i = 0; i < numHouses; i++) {
		const House *house = &houses[i];
		const DoorTrigger *door = &house->door;

		/* Scale door offset and size by HOUSE_SCALE */
		int32_t scaledOffsetX = (door->offsetX * HOUSE_SCALE) >> 12;
		int32_t scaledOffsetZ = (door->offsetZ * HOUSE_SCALE) >> 12;
		int32_t scaledSizeX = (door->sizeX * HOUSE_SCALE) >> 12;
		int32_t scaledSizeZ = (door->sizeZ * HOUSE_SCALE) >> 12;

		/* Rotate door offset based on house rotation (90° intervals)
		 * Normalize rotation to 0-4095 range first */
		int16_t rot = house->rotation;
		while (rot < 0) rot += 4096;
		while (rot >= 4096) rot -= 4096;

		int32_t worldOffsetX, worldOffsetZ;
		if (rot < 512) {
			/* ~0° */
			worldOffsetX = scaledOffsetX;
			worldOffsetZ = scaledOffsetZ;
		} else if (rot < 1536) {
			/* ~90° */
			worldOffsetX = scaledOffsetZ;
			worldOffsetZ = -scaledOffsetX;
		} else if (rot < 2560) {
			/* ~180° */
			worldOffsetX = -scaledOffsetX;
			worldOffsetZ = -scaledOffsetZ;
		} else if (rot < 3584) {
			/* ~270° */
			worldOffsetX = -scaledOffsetZ;
			worldOffsetZ = scaledOffsetX;
		} else {
			/* ~360° (wraps to ~0°) */
			worldOffsetX = scaledOffsetX;
			worldOffsetZ = scaledOffsetZ;
		}

		/* Calculate door bounds in world space */
		int32_t doorCenterX = house->x + worldOffsetX;
		int32_t doorCenterZ = house->z + worldOffsetZ;

		/* Simple AABB point test (door doesn't rotate, stays axis-aligned) */
		if (playerX >= doorCenterX - scaledSizeX &&
		    playerX <= doorCenterX + scaledSizeX &&
		    playerZ >= doorCenterZ - scaledSizeZ &&
		    playerZ <= doorCenterZ + scaledSizeZ) {
			return i;
		}
	}
	return -1;
}

#if DEBUG_DRAW_COLLISION
/* Screen constants for manual projection (must match GTE settings) */
#define DEBUG_CENTERX 160  /* 320 / 2 */
#define DEBUG_CENTERY 120  /* 240 / 2 */

/* Draw a single 2D line */
static void drawLine2D(DMAChain *chain, int zIndex,
                       int16_t x1, int16_t y1, int16_t x2, int16_t y2,
                       uint8_t r, uint8_t g, uint8_t b) {
	if (zIndex < 0 || zIndex >= ORDERING_TABLE_SIZE) return;

	/* Clip to screen bounds (simple rejection) */
	if (x1 < -512 || x1 > 512 || y1 < -512 || y1 > 512) return;
	if (x2 < -512 || x2 > 512 || y2 < -512 || y2 > 512) return;

	uint32_t *ptr = allocatePacket(chain, zIndex, 3);
	ptr[0] = gp0_rgb(r, g, b) | gp0_line(false, false);
	ptr[1] = gp0_xy(x1, y1);
	ptr[2] = gp0_xy(x2, y2);
}

/* Draw wireframe collision boxes for a house */
static void drawHouseCollisionDebug(DMAChain *chain, const House *house, const Camera *cam) {
	/* Early distance cull - skip houses too far from camera */
	int32_t dx = house->x - cam->x;
	int32_t dz = house->z - cam->z;
	if (dx > CULL_DISTANCE_DEBUG || dx < -CULL_DISTANCE_DEBUG ||
	    dz > CULL_DISTANCE_DEBUG || dz < -CULL_DISTANCE_DEBUG) return;

	for (int i = 0; i < house->numCollisionBoxes; i++) {
		const CollisionBox *box = &house->collisionBoxes[i];

		/* Scale box by HOUSE_SCALE */
		int32_t scaledMinX = (box->minX * HOUSE_SCALE) >> 12;
		int32_t scaledMinZ = (box->minZ * HOUSE_SCALE) >> 12;
		int32_t scaledMaxX = (box->maxX * HOUSE_SCALE) >> 12;
		int32_t scaledMaxZ = (box->maxZ * HOUSE_SCALE) >> 12;

		/* Box corners in world space (at floor level) */
		int32_t worldY = FLOOR_Y;

		/* 4 corners of the box */
		int32_t corners[4][3] = {
			{house->x + scaledMinX, worldY, house->z + scaledMinZ},
			{house->x + scaledMaxX, worldY, house->z + scaledMinZ},
			{house->x + scaledMaxX, worldY, house->z + scaledMaxZ},
			{house->x + scaledMinX, worldY, house->z + scaledMaxZ}
		};

		/* Transform corners to view space and project manually */
		int16_t screenX[4], screenY[4];
		int32_t avgZ = 0;
		bool allVisible = true;

		for (int c = 0; c < 4; c++) {
			/* Relative to camera */
			int32_t relX = corners[c][0] - cam->x;
			int32_t relY = corners[c][1] - cam->y;
			int32_t relZ = corners[c][2] - cam->z;

			/* Apply camera view rotation */
			int32_t viewX = ((int32_t)cam->viewRotation.m[0][0] * relX +
			                 (int32_t)cam->viewRotation.m[0][1] * relY +
			                 (int32_t)cam->viewRotation.m[0][2] * relZ) >> FP_SHIFT;
			int32_t viewY = ((int32_t)cam->viewRotation.m[1][0] * relX +
			                 (int32_t)cam->viewRotation.m[1][1] * relY +
			                 (int32_t)cam->viewRotation.m[1][2] * relZ) >> FP_SHIFT;
			int32_t viewZ = ((int32_t)cam->viewRotation.m[2][0] * relX +
			                 (int32_t)cam->viewRotation.m[2][1] * relY +
			                 (int32_t)cam->viewRotation.m[2][2] * relZ) >> FP_SHIFT;

			/* Skip if behind camera */
			if (viewZ < 10) {
				allVisible = false;
				break;
			}

			avgZ += viewZ;

			/* Manual perspective projection (matches GTE H register):
			 * screenX = (viewX * focalLength / viewZ) + centerX
			 * screenY = (viewY * focalLength / viewZ) + centerY */
			screenX[c] = (int16_t)((viewX * CAMERA_FOCAL_LENGTH) / viewZ + DEBUG_CENTERX);
			screenY[c] = (int16_t)((viewY * CAMERA_FOCAL_LENGTH) / viewZ + DEBUG_CENTERY);
		}

		if (!allVisible) continue;

		/* Calculate z-index for ordering table */
		int zIndex = (avgZ / 4) / (32768 / ORDERING_TABLE_SIZE);
		if (zIndex >= ORDERING_TABLE_SIZE) zIndex = ORDERING_TABLE_SIZE - 1;
		if (zIndex < 0) zIndex = 0;

		/* Draw 4 lines to form the box outline (bright green) */
		drawLine2D(chain, zIndex, screenX[0], screenY[0], screenX[1], screenY[1], 0, 255, 0);
		drawLine2D(chain, zIndex, screenX[1], screenY[1], screenX[2], screenY[2], 0, 255, 0);
		drawLine2D(chain, zIndex, screenX[2], screenY[2], screenX[3], screenY[3], 0, 255, 0);
		drawLine2D(chain, zIndex, screenX[3], screenY[3], screenX[0], screenY[0], 0, 255, 0);
	}
}

/* Draw collision debug for all houses */
static void drawAllCollisionDebug(DMAChain *chain, const House *houses, int numHouses,
                                   const Camera *cam) {
	for (int i = 0; i < numHouses; i++) {
		drawHouseCollisionDebug(chain, &houses[i], cam);
	}
}

/* Draw door trigger debug wireframe (red) */
static void drawDoorTriggerDebug(DMAChain *chain, const House *house, const Camera *cam) {
	/* Early distance cull - skip houses too far from camera */
	int32_t dx = house->x - cam->x;
	int32_t dz = house->z - cam->z;
	if (dx > CULL_DISTANCE_DEBUG || dx < -CULL_DISTANCE_DEBUG ||
	    dz > CULL_DISTANCE_DEBUG || dz < -CULL_DISTANCE_DEBUG) return;

	const DoorTrigger *door = &house->door;

	/* Scale door offset and size by HOUSE_SCALE */
	int32_t scaledOffsetX = (door->offsetX * HOUSE_SCALE) >> 12;
	int32_t scaledOffsetZ = (door->offsetZ * HOUSE_SCALE) >> 12;
	int32_t scaledSizeX = (door->sizeX * HOUSE_SCALE) >> 12;
	int32_t scaledSizeZ = (door->sizeZ * HOUSE_SCALE) >> 12;

	/* Rotate door offset based on house rotation (90° intervals) */
	int16_t rot = house->rotation;
	while (rot < 0) rot += 4096;
	while (rot >= 4096) rot -= 4096;

	int32_t worldOffsetX, worldOffsetZ;
	if (rot < 512) {
		worldOffsetX = scaledOffsetX;
		worldOffsetZ = scaledOffsetZ;
	} else if (rot < 1536) {
		worldOffsetX = scaledOffsetZ;
		worldOffsetZ = -scaledOffsetX;
	} else if (rot < 2560) {
		worldOffsetX = -scaledOffsetX;
		worldOffsetZ = -scaledOffsetZ;
	} else if (rot < 3584) {
		worldOffsetX = -scaledOffsetZ;
		worldOffsetZ = scaledOffsetX;
	} else {
		worldOffsetX = scaledOffsetX;
		worldOffsetZ = scaledOffsetZ;
	}

	/* Calculate door center in world space */
	int32_t doorCenterX = house->x + worldOffsetX;
	int32_t doorCenterZ = house->z + worldOffsetZ;
	int32_t worldY = FLOOR_Y;

	/* 4 corners of the door trigger */
	int32_t corners[4][3] = {
		{doorCenterX - scaledSizeX, worldY, doorCenterZ - scaledSizeZ},
		{doorCenterX + scaledSizeX, worldY, doorCenterZ - scaledSizeZ},
		{doorCenterX + scaledSizeX, worldY, doorCenterZ + scaledSizeZ},
		{doorCenterX - scaledSizeX, worldY, doorCenterZ + scaledSizeZ}
	};

	/* Transform corners to view space and project */
	int16_t screenX[4], screenY[4];
	int32_t avgZ = 0;
	bool allVisible = true;

	for (int c = 0; c < 4; c++) {
		int32_t relX = corners[c][0] - cam->x;
		int32_t relY = corners[c][1] - cam->y;
		int32_t relZ = corners[c][2] - cam->z;

		int32_t viewX = ((int32_t)cam->viewRotation.m[0][0] * relX +
		                 (int32_t)cam->viewRotation.m[0][1] * relY +
		                 (int32_t)cam->viewRotation.m[0][2] * relZ) >> FP_SHIFT;
		int32_t viewY = ((int32_t)cam->viewRotation.m[1][0] * relX +
		                 (int32_t)cam->viewRotation.m[1][1] * relY +
		                 (int32_t)cam->viewRotation.m[1][2] * relZ) >> FP_SHIFT;
		int32_t viewZ = ((int32_t)cam->viewRotation.m[2][0] * relX +
		                 (int32_t)cam->viewRotation.m[2][1] * relY +
		                 (int32_t)cam->viewRotation.m[2][2] * relZ) >> FP_SHIFT;

		if (viewZ < 10) {
			allVisible = false;
			break;
		}

		avgZ += viewZ;
		screenX[c] = (int16_t)((viewX * CAMERA_FOCAL_LENGTH) / viewZ + DEBUG_CENTERX);
		screenY[c] = (int16_t)((viewY * CAMERA_FOCAL_LENGTH) / viewZ + DEBUG_CENTERY);
	}

	if (!allVisible) return;

	int zIndex = (avgZ / 4) / (32768 / ORDERING_TABLE_SIZE);
	if (zIndex >= ORDERING_TABLE_SIZE) zIndex = ORDERING_TABLE_SIZE - 1;
	if (zIndex < 0) zIndex = 0;

	/* Draw 4 lines to form the door trigger outline (bright red) */
	drawLine2D(chain, zIndex, screenX[0], screenY[0], screenX[1], screenY[1], 255, 0, 0);
	drawLine2D(chain, zIndex, screenX[1], screenY[1], screenX[2], screenY[2], 255, 0, 0);
	drawLine2D(chain, zIndex, screenX[2], screenY[2], screenX[3], screenY[3], 255, 0, 0);
	drawLine2D(chain, zIndex, screenX[3], screenY[3], screenX[0], screenY[0], 255, 0, 0);
}

/* Draw door trigger debug for all houses */
static void drawAllDoorTriggersDebug(DMAChain *chain, const House *houses, int numHouses,
                                      const Camera *cam) {
	for (int i = 0; i < numHouses; i++) {
		drawDoorTriggerDebug(chain, &houses[i], cam);
	}
}

/* Draw a rectangular outline in world space (for interior debug) */
static void drawRectDebug(DMAChain *chain, const Camera *cam,
                          int32_t centerX, int32_t centerZ, int32_t halfX, int32_t halfZ,
                          uint8_t r, uint8_t g, uint8_t b) {
	int32_t worldY = FLOOR_Y;

	/* 4 corners of the rectangle */
	int32_t corners[4][3] = {
		{centerX - halfX, worldY, centerZ - halfZ},
		{centerX + halfX, worldY, centerZ - halfZ},
		{centerX + halfX, worldY, centerZ + halfZ},
		{centerX - halfX, worldY, centerZ + halfZ}
	};

	/* Transform corners to view space and project */
	int16_t screenX[4], screenY[4];
	int32_t avgZ = 0;
	bool allVisible = true;

	for (int c = 0; c < 4; c++) {
		int32_t relX = corners[c][0] - cam->x;
		int32_t relY = corners[c][1] - cam->y;
		int32_t relZ = corners[c][2] - cam->z;

		int32_t viewX = ((int32_t)cam->viewRotation.m[0][0] * relX +
		                 (int32_t)cam->viewRotation.m[0][1] * relY +
		                 (int32_t)cam->viewRotation.m[0][2] * relZ) >> FP_SHIFT;
		int32_t viewY = ((int32_t)cam->viewRotation.m[1][0] * relX +
		                 (int32_t)cam->viewRotation.m[1][1] * relY +
		                 (int32_t)cam->viewRotation.m[1][2] * relZ) >> FP_SHIFT;
		int32_t viewZ = ((int32_t)cam->viewRotation.m[2][0] * relX +
		                 (int32_t)cam->viewRotation.m[2][1] * relY +
		                 (int32_t)cam->viewRotation.m[2][2] * relZ) >> FP_SHIFT;

		if (viewZ < 10) {
			allVisible = false;
			break;
		}

		avgZ += viewZ;
		screenX[c] = (int16_t)((viewX * CAMERA_FOCAL_LENGTH) / viewZ + DEBUG_CENTERX);
		screenY[c] = (int16_t)((viewY * CAMERA_FOCAL_LENGTH) / viewZ + DEBUG_CENTERY);
	}

	if (!allVisible) return;

	int zIndex = (avgZ / 4) / (32768 / ORDERING_TABLE_SIZE);
	if (zIndex >= ORDERING_TABLE_SIZE) zIndex = ORDERING_TABLE_SIZE - 1;
	if (zIndex < 0) zIndex = 0;

	/* Draw 4 lines to form the rectangle outline */
	drawLine2D(chain, zIndex, screenX[0], screenY[0], screenX[1], screenY[1], r, g, b);
	drawLine2D(chain, zIndex, screenX[1], screenY[1], screenX[2], screenY[2], r, g, b);
	drawLine2D(chain, zIndex, screenX[2], screenY[2], screenX[3], screenY[3], r, g, b);
	drawLine2D(chain, zIndex, screenX[3], screenY[3], screenX[0], screenY[0], r, g, b);
}

/* Draw interior debug (floor bounds and door trigger) */
static void drawInteriorDebug(DMAChain *chain, const Camera *cam,
                              int32_t doorOffsetX, int32_t doorOffsetZ,
                              int32_t doorSizeX, int32_t doorSizeZ) {
	/* Draw floor bounds (cyan) */
	drawRectDebug(chain, cam, 0, 0,
	              INTERIOR_FLOOR_HALF_X, INTERIOR_FLOOR_HALF_Z,
	              0, 255, 255);

	/* Draw door trigger (red) */
	drawRectDebug(chain, cam, doorOffsetX, doorOffsetZ,
	              doorSizeX, doorSizeZ,
	              255, 0, 0);
}
#endif /* DEBUG_DRAW_COLLISION */

/* Font data embedded by CMake */
extern const uint8_t fontTexture[];
extern const uint8_t fontPalette[];

/* Music data embedded by CMake (SPU-ADPCM format) */
extern const uint8_t musicData[];
extern const uint32_t musicData_size;

/* Font dimensions */
#define FONT_WIDTH        96
#define FONT_HEIGHT       56
#define FONT_COLOR_DEPTH  GP0_COLOR_4BPP

/* Controller button definitions */
#define PAD_SELECT   (1 << 0)
#define PAD_L3       (1 << 1)
#define PAD_R3       (1 << 2)
#define PAD_START    (1 << 3)
#define PAD_UP       (1 << 4)
#define PAD_RIGHT    (1 << 5)
#define PAD_DOWN     (1 << 6)
#define PAD_LEFT     (1 << 7)
#define PAD_L2       (1 << 8)
#define PAD_R2       (1 << 9)
#define PAD_L1       (1 << 10)
#define PAD_R1       (1 << 11)
#define PAD_TRIANGLE (1 << 12)
#define PAD_CIRCLE   (1 << 13)
#define PAD_X        (1 << 14)
#define PAD_SQUARE   (1 << 15)

/* GTE uses 20.12 fixed-point format */
#define ONE (1 << 12)

/* Screen resolution */
#define SCREEN_WIDTH  320
#define SCREEN_HEIGHT 240

/* Screen center position */
#define CENTERX (SCREEN_WIDTH  / 2)
#define CENTERY (SCREEN_HEIGHT / 2)

/* Initialize the GTE for 3D rendering */
static void setupGTE(int width, int height) {
	/* Enable coprocessor 2 (GTE) */
	cop0_setReg(COP0_STATUS, cop0_getReg(COP0_STATUS) | COP0_STATUS_CU2);

	/* Set screen offset (center of screen) - 16.16 fixed-point */
	gte_setControlReg(GTE_OFX, (width  << 16) / 2);
	gte_setControlReg(GTE_OFY, (height << 16) / 2);

	/* Set projection plane distance (FOV control)
	 * Higher value = narrower FOV (more zoomed in) */
	gte_setControlReg(GTE_H, CAMERA_FOCAL_LENGTH);

	/* Set Z averaging scale factors for ordering table sorting */
	gte_setControlReg(GTE_ZSF3, ORDERING_TABLE_SIZE / 3);
	gte_setControlReg(GTE_ZSF4, ORDERING_TABLE_SIZE / 4);
}

/* Initialize Timer 2 for frame timing (runs at CPU clock / 8 = ~4.2 MHz) */
static void setupTimer(void) {
	TIMER_CTRL(2) = 0;                        /* Stop timer */
	TIMER_VALUE(2) = 0;                       /* Reset counter */
	TIMER_CTRL(2) = TIMER_CTRL_PRESCALE;      /* CPU/8, free running */
}

/* Draw grass floor with noise-based color variation */
static void drawFloor(DMAChain *chain, const Camera *cam) {
	/* Set up identity rotation matrix (we rotate manually) */
	gte_setRotationMatrix(
		ONE, 0, 0,
		0, ONE, 0,
		0, 0, ONE
	);

	/* Calculate which tiles are visible based on camera position */
	int baseTileX = cam->x / FLOOR_TILE_SIZE;
	int baseTileZ = cam->z / FLOOR_TILE_SIZE;

	/* Draw grid of floor tiles */
	for (int tz = -FLOOR_GRID_SIZE; tz < FLOOR_GRID_SIZE; tz++) {
		for (int tx = -FLOOR_GRID_SIZE; tx < FLOOR_GRID_SIZE; tx++) {
			int tileX = baseTileX + tx;
			int tileZ = baseTileZ + tz;

			/* Calculate world position of tile corners relative to camera */
			int32_t x0 = tileX * FLOOR_TILE_SIZE - cam->x;
			int32_t x1 = x0 + FLOOR_TILE_SIZE;
			int32_t z0 = tileZ * FLOOR_TILE_SIZE - cam->z;
			int32_t z1 = z0 + FLOOR_TILE_SIZE;
			int32_t y = FLOOR_Y - cam->y;

			/* Convert tile to map pixel coordinates for street lookup
			 * Tile center world pos: (tileX * FLOOR_TILE_SIZE + FLOOR_TILE_SIZE/2)
			 * Map pixel: (worldX + MAP_WORLD_SIZE/2) / MAP_SCALE */
			int32_t tileCenterX = tileX * FLOOR_TILE_SIZE + FLOOR_TILE_SIZE / 2;
			int32_t tileCenterZ = tileZ * FLOOR_TILE_SIZE + FLOOR_TILE_SIZE / 2;
			int mapPixelX = (tileCenterX + MAP_WORLD_SIZE / 2) / MAP_SCALE;
			int mapPixelZ = (tileCenterZ + MAP_WORLD_SIZE / 2) / MAP_SCALE;

			/* Check if this tile is a street */
			uint8_t r, g, b;
			if (IS_STREET_PIXEL(mapPixelX, mapPixelZ)) {
				/* Street tile - use grey colors */
				int noise = tileNoise(tileX, tileZ);
				if (noise < 128) {
					r = STREET_COLOR_1_R; g = STREET_COLOR_1_G; b = STREET_COLOR_1_B;
				} else {
					r = STREET_COLOR_2_R; g = STREET_COLOR_2_G; b = STREET_COLOR_2_B;
				}
			} else {
				/* Grass tile - use noise to select color variant */
				int noise = tileNoise(tileX, tileZ);
				if (noise < 100) {
					/* Teal grass - most common */
					r = GRASS_COLOR_1_R; g = GRASS_COLOR_1_G; b = GRASS_COLOR_1_B;
				} else if (noise < 180) {
					/* Bright green grass */
					r = GRASS_COLOR_2_R; g = GRASS_COLOR_2_G; b = GRASS_COLOR_2_B;
				} else {
					/* Dark blue-ish patches (shadows/variety) */
					r = GRASS_COLOR_3_R; g = GRASS_COLOR_3_G; b = GRASS_COLOR_3_B;
				}

				/* Add slight variation based on secondary noise */
				int variation = tileNoise(tileX + 1000, tileZ + 1000) >> 5;  /* 0-7 */
				r = (r + variation > 255) ? 255 : r + variation;
				g = (g + variation > 255) ? 255 : g + variation;
			}

			/* Set translation for GTE */
			gte_setControlReg(GTE_TRX, 0);
			gte_setControlReg(GTE_TRY, 0);
			gte_setControlReg(GTE_TRZ, 0);

			/* Transform floor vertices using camera's full view rotation matrix */
			/* Corner 0 (x0, y, z0) */
			int32_t vx0 = ((int32_t)cam->viewRotation.m[0][0] * x0 +
			               (int32_t)cam->viewRotation.m[0][1] * y +
			               (int32_t)cam->viewRotation.m[0][2] * z0) >> FP_SHIFT;
			int32_t vy0 = ((int32_t)cam->viewRotation.m[1][0] * x0 +
			               (int32_t)cam->viewRotation.m[1][1] * y +
			               (int32_t)cam->viewRotation.m[1][2] * z0) >> FP_SHIFT;
			int32_t vz0 = ((int32_t)cam->viewRotation.m[2][0] * x0 +
			               (int32_t)cam->viewRotation.m[2][1] * y +
			               (int32_t)cam->viewRotation.m[2][2] * z0) >> FP_SHIFT;

			/* Corner 1 (x1, y, z0) */
			int32_t vx1 = ((int32_t)cam->viewRotation.m[0][0] * x1 +
			               (int32_t)cam->viewRotation.m[0][1] * y +
			               (int32_t)cam->viewRotation.m[0][2] * z0) >> FP_SHIFT;
			int32_t vy1 = ((int32_t)cam->viewRotation.m[1][0] * x1 +
			               (int32_t)cam->viewRotation.m[1][1] * y +
			               (int32_t)cam->viewRotation.m[1][2] * z0) >> FP_SHIFT;
			int32_t vz1 = ((int32_t)cam->viewRotation.m[2][0] * x1 +
			               (int32_t)cam->viewRotation.m[2][1] * y +
			               (int32_t)cam->viewRotation.m[2][2] * z0) >> FP_SHIFT;

			/* Corner 2 (x1, y, z1) */
			int32_t vx2 = ((int32_t)cam->viewRotation.m[0][0] * x1 +
			               (int32_t)cam->viewRotation.m[0][1] * y +
			               (int32_t)cam->viewRotation.m[0][2] * z1) >> FP_SHIFT;
			int32_t vy2 = ((int32_t)cam->viewRotation.m[1][0] * x1 +
			               (int32_t)cam->viewRotation.m[1][1] * y +
			               (int32_t)cam->viewRotation.m[1][2] * z1) >> FP_SHIFT;
			int32_t vz2 = ((int32_t)cam->viewRotation.m[2][0] * x1 +
			               (int32_t)cam->viewRotation.m[2][1] * y +
			               (int32_t)cam->viewRotation.m[2][2] * z1) >> FP_SHIFT;

			/* Corner 3 (x0, y, z1) */
			int32_t vx3 = ((int32_t)cam->viewRotation.m[0][0] * x0 +
			               (int32_t)cam->viewRotation.m[0][1] * y +
			               (int32_t)cam->viewRotation.m[0][2] * z1) >> FP_SHIFT;
			int32_t vy3 = ((int32_t)cam->viewRotation.m[1][0] * x0 +
			               (int32_t)cam->viewRotation.m[1][1] * y +
			               (int32_t)cam->viewRotation.m[1][2] * z1) >> FP_SHIFT;
			int32_t vz3 = ((int32_t)cam->viewRotation.m[2][0] * x0 +
			               (int32_t)cam->viewRotation.m[2][1] * y +
			               (int32_t)cam->viewRotation.m[2][2] * z1) >> FP_SHIFT;

			/* Frustum culling in view space (with margin to avoid popping at edges)
			 * Use wider angle than actual FOV: vx*2 vs vz*3 gives ~56 degree half-angle */
			/* Skip tiles behind camera */
			if (vz0 < 10 && vz1 < 10 && vz2 < 10 && vz3 < 10) continue;

			/* Skip tiles entirely to the left of view */
			if (vx0*2 < -vz0*3 && vx1*2 < -vz1*3 && vx2*2 < -vz2*3 && vx3*2 < -vz3*3) continue;

			/* Skip tiles entirely to the right of view */
			if (vx0*2 > vz0*3 && vx1*2 > vz1*3 && vx2*2 > vz2*3 && vx3*2 > vz3*3) continue;

			/* Transform 4 corners of tile with rotated coordinates */
			GTEVector16 v0 = {vx0, vy0, vz0, 0};
			GTEVector16 v1 = {vx1, vy1, vz1, 0};
			GTEVector16 v2 = {vx2, vy2, vz2, 0};
			GTEVector16 v3 = {vx3, vy3, vz3, 0};

			/* Triangle 1: v0, v1, v2 */
			gte_loadV0(&v0);
			gte_loadV1(&v1);
			gte_loadV2(&v2);
			gte_command(GTE_CMD_RTPT | GTE_SF);

			/* Count this tile */
			statTiles++;

			/* Floor always draws at back of ordering table (before models)
			 * Use fixed z-index instead of depth sorting to avoid z-fighting */
			uint32_t *ptr = allocatePacket(chain, ORDERING_TABLE_SIZE - 2, 4);
			ptr[0] = gp0_rgb(r, g, b) | gp0_triangle(false, false);
			gte_storeDataReg(GTE_SXY0, 1 * 4, ptr);
			gte_storeDataReg(GTE_SXY1, 2 * 4, ptr);
			gte_storeDataReg(GTE_SXY2, 3 * 4, ptr);
			statTriangles++;

			/* Triangle 2: v0, v2, v3 */
			gte_loadV0(&v0);
			gte_loadV1(&v2);
			gte_loadV2(&v3);
			gte_command(GTE_CMD_RTPT | GTE_SF);

			ptr = allocatePacket(chain, ORDERING_TABLE_SIZE - 2, 4);
			ptr[0] = gp0_rgb(r, g, b) | gp0_triangle(false, false);
			gte_storeDataReg(GTE_SXY0, 1 * 4, ptr);
			gte_storeDataReg(GTE_SXY1, 2 * 4, ptr);
			statTriangles++;
			gte_storeDataReg(GTE_SXY2, 3 * 4, ptr);
		}
	}
}

/* Draw a static house model at world position */
static void drawHouse(DMAChain *chain, const House *house, const Camera *cam) {
	/* Calculate house position relative to camera (in world space) */
	int32_t relX = house->x - cam->x;
	int32_t relY = house->y - cam->y;
	int32_t relZ = house->z - cam->z;

	/* Early distance cull - skip houses too far from camera (before expensive matrix ops) */
	if (relX > CULL_DISTANCE_HOUSE || relX < -CULL_DISTANCE_HOUSE ||
	    relZ > CULL_DISTANCE_HOUSE || relZ < -CULL_DISTANCE_HOUSE) return;

	/* Apply camera's view rotation matrix to get view-space position */
	int32_t viewX = ((int32_t)cam->viewRotation.m[0][0] * relX +
	                 (int32_t)cam->viewRotation.m[0][1] * relY +
	                 (int32_t)cam->viewRotation.m[0][2] * relZ) >> FP_SHIFT;
	int32_t viewY = ((int32_t)cam->viewRotation.m[1][0] * relX +
	                 (int32_t)cam->viewRotation.m[1][1] * relY +
	                 (int32_t)cam->viewRotation.m[1][2] * relZ) >> FP_SHIFT;
	int32_t viewZ = ((int32_t)cam->viewRotation.m[2][0] * relX +
	                 (int32_t)cam->viewRotation.m[2][1] * relY +
	                 (int32_t)cam->viewRotation.m[2][2] * relZ) >> FP_SHIFT;

	/* Skip if behind camera */
	if (viewZ < 10) return;

	/* Build combined rotation: viewRotation * houseYawRotation
	 * This properly handles camera pitch so houses don't bob */
	Matrix3x3 houseRot;
	matrixRotateY(&houseRot, house->rotation);

	Matrix3x3 combined;
	matrixMultiply(&combined, &cam->viewRotation, &houseRot);

	/* Load combined rotation matrix to GTE */
	matrixLoadToGTE(&combined);

	/* Set translation */
	gte_setControlReg(GTE_TRX, viewX);
	gte_setControlReg(GTE_TRY, viewY);
	gte_setControlReg(GTE_TRZ, viewZ);

	/* Draw all faces */
	const Model *model = &house->model;
	for (int i = 0; i < model->numFaces; i++) {
		const Face *face = &model->faces[i];

		/* Scale vertices by HOUSE_SCALE (4096 = 1.0x) */
		GTEVector16 v0, v1, v2;
		v0.x = (model->vertices[face->v0].x * HOUSE_SCALE) >> 12;
		v0.y = (model->vertices[face->v0].y * HOUSE_SCALE) >> 12;
		v0.z = (model->vertices[face->v0].z * HOUSE_SCALE) >> 12;
		v0._padding = 0;

		v1.x = (model->vertices[face->v1].x * HOUSE_SCALE) >> 12;
		v1.y = (model->vertices[face->v1].y * HOUSE_SCALE) >> 12;
		v1.z = (model->vertices[face->v1].z * HOUSE_SCALE) >> 12;
		v1._padding = 0;

		v2.x = (model->vertices[face->v2].x * HOUSE_SCALE) >> 12;
		v2.y = (model->vertices[face->v2].y * HOUSE_SCALE) >> 12;
		v2.z = (model->vertices[face->v2].z * HOUSE_SCALE) >> 12;
		v2._padding = 0;

		/* Load scaled vertices */
		gte_loadV0(&v0);
		gte_loadV1(&v1);
		gte_loadV2(&v2);

		/* Perspective transformation */
		gte_command(GTE_CMD_RTPT | GTE_SF);

		/* Backface culling */
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
			r0 = g0 = b0 = r1 = g1 = b1 = r2 = g2 = b2 = 128;
		}

		/* Allocate packet for Gouraud-shaded triangle */
		uint32_t *ptr = allocatePacket(chain, zIndex, 6);
		ptr[0] = gp0_rgb(r0, g0, b0) | gp0_shadedTriangle(true, false, false);
		gte_storeDataReg(GTE_SXY0, 1 * 4, ptr);
		ptr[2] = gp0_rgb(r1, g1, b1);
		gte_storeDataReg(GTE_SXY1, 3 * 4, ptr);
		ptr[4] = gp0_rgb(r2, g2, b2);
		gte_storeDataReg(GTE_SXY2, 5 * 4, ptr);

		statTriangles++;
	}
}

/* Set up GTE for tree batch rendering (call once before drawing all trees) */
static void setupTreeBatch(const Camera *cam) {
	/* Load camera view rotation to GTE - trees have no object rotation */
	matrixLoadToGTE(&cam->viewRotation);
}

/* Draw a tree model at world position (no rotation) - call setupTreeBatch first */
static void drawTree(DMAChain *chain, const Tree *tree, const Camera *cam) {
	/* Calculate tree position relative to camera (in world space) */
	int32_t relX = tree->x - cam->x;
	int32_t relY = tree->y - cam->y;
	int32_t relZ = tree->z - cam->z;

	/* Early distance cull - skip trees too far from camera (before expensive matrix ops) */
	if (relX > CULL_DISTANCE_TREE || relX < -CULL_DISTANCE_TREE ||
	    relZ > CULL_DISTANCE_TREE || relZ < -CULL_DISTANCE_TREE) return;

	/* Apply camera's view rotation matrix to get view-space position */
	int32_t viewX = ((int32_t)cam->viewRotation.m[0][0] * relX +
	                 (int32_t)cam->viewRotation.m[0][1] * relY +
	                 (int32_t)cam->viewRotation.m[0][2] * relZ) >> FP_SHIFT;
	int32_t viewY = ((int32_t)cam->viewRotation.m[1][0] * relX +
	                 (int32_t)cam->viewRotation.m[1][1] * relY +
	                 (int32_t)cam->viewRotation.m[1][2] * relZ) >> FP_SHIFT;
	int32_t viewZ = ((int32_t)cam->viewRotation.m[2][0] * relX +
	                 (int32_t)cam->viewRotation.m[2][1] * relY +
	                 (int32_t)cam->viewRotation.m[2][2] * relZ) >> FP_SHIFT;

	/* Skip if behind camera */
	if (viewZ < 10) return;

	/* GTE rotation matrix already set by setupTreeBatch - just set translation */
	gte_setControlReg(GTE_TRX, viewX);
	gte_setControlReg(GTE_TRY, viewY);
	gte_setControlReg(GTE_TRZ, viewZ);

	/* Draw all faces */
	const Model *model = &tree->model;
	for (int i = 0; i < model->numFaces; i++) {
		const Face *face = &model->faces[i];

		/* Scale vertices by TREE_SCALE (4096 = 1.0x) */
		GTEVector16 v0, v1, v2;
		v0.x = (model->vertices[face->v0].x * TREE_SCALE) >> 12;
		v0.y = (model->vertices[face->v0].y * TREE_SCALE) >> 12;
		v0.z = (model->vertices[face->v0].z * TREE_SCALE) >> 12;
		v0._padding = 0;

		v1.x = (model->vertices[face->v1].x * TREE_SCALE) >> 12;
		v1.y = (model->vertices[face->v1].y * TREE_SCALE) >> 12;
		v1.z = (model->vertices[face->v1].z * TREE_SCALE) >> 12;
		v1._padding = 0;

		v2.x = (model->vertices[face->v2].x * TREE_SCALE) >> 12;
		v2.y = (model->vertices[face->v2].y * TREE_SCALE) >> 12;
		v2.z = (model->vertices[face->v2].z * TREE_SCALE) >> 12;
		v2._padding = 0;

		/* Load scaled vertices */
		gte_loadV0(&v0);
		gte_loadV1(&v1);
		gte_loadV2(&v2);

		/* Perspective transformation */
		gte_command(GTE_CMD_RTPT | GTE_SF);

		/* Backface culling */
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
			/* Default green for trees */
			r0 = g0 = b0 = r1 = g1 = b1 = r2 = g2 = b2 = 80;
		}

		/* Allocate packet for Gouraud-shaded triangle */
		uint32_t *ptr = allocatePacket(chain, zIndex, 6);
		ptr[0] = gp0_rgb(r0, g0, b0) | gp0_shadedTriangle(true, false, false);
		gte_storeDataReg(GTE_SXY0, 1 * 4, ptr);
		ptr[2] = gp0_rgb(r1, g1, b1);
		gte_storeDataReg(GTE_SXY1, 3 * 4, ptr);
		ptr[4] = gp0_rgb(r2, g2, b2);
		gte_storeDataReg(GTE_SXY2, 5 * 4, ptr);

		statTriangles++;
	}
}

/* Set up GTE for fence batch rendering (call once before drawing all fences) */
static void setupFenceBatch(const Camera *cam) {
	/* Load camera view rotation to GTE - fences have no object rotation */
	matrixLoadToGTE(&cam->viewRotation);
}

/* Draw a fence post as a tile-aligned vertical quad (two triangles, two-sided) */
static void drawFencePost(DMAChain *chain, const FencePost *post, const Camera *cam) {
	/* Quick distance check */
	int32_t dx = post->x - cam->x;
	int32_t dz = post->z - cam->z;

	if (dx > CULL_DISTANCE_FENCE || dx < -CULL_DISTANCE_FENCE ||
	    dz > CULL_DISTANCE_FENCE || dz < -CULL_DISTANCE_FENCE) return;

	/* Calculate view-space position */
	int32_t relX = post->x - cam->x;
	int32_t relY = FLOOR_Y - cam->y;
	int32_t relZ = post->z - cam->z;

	int32_t viewX = ((int32_t)cam->viewRotation.m[0][0] * relX +
	                 (int32_t)cam->viewRotation.m[0][1] * relY +
	                 (int32_t)cam->viewRotation.m[0][2] * relZ) >> FP_SHIFT;
	int32_t viewY = ((int32_t)cam->viewRotation.m[1][0] * relX +
	                 (int32_t)cam->viewRotation.m[1][1] * relY +
	                 (int32_t)cam->viewRotation.m[1][2] * relZ) >> FP_SHIFT;
	int32_t viewZ = ((int32_t)cam->viewRotation.m[2][0] * relX +
	                 (int32_t)cam->viewRotation.m[2][1] * relY +
	                 (int32_t)cam->viewRotation.m[2][2] * relZ) >> FP_SHIFT;

	if (viewZ < 10) return;

	/* Set GTE translation */
	gte_setControlReg(GTE_TRX, viewX);
	gte_setControlReg(GTE_TRY, viewY);
	gte_setControlReg(GTE_TRZ, viewZ);

	/* Fence is a flat quad spanning the full map tile (MAP_SCALE x FENCE_HEIGHT) */
	int16_t halfWidth = MAP_SCALE / 2;
	/* Y=0 is at floor level, negative Y is above the floor */
	int16_t topY = -FENCE_HEIGHT;
	int16_t botY = 0;

	uint32_t *ptr;
	int zIndex;

	/* Wall 1: X-aligned wall (spans -X to +X, at Z=0) - double sided */
	/* Front face */
	GTEVector16 x0 = {-halfWidth, botY, 0, 0};  /* Bottom-left */
	GTEVector16 x1 = {halfWidth, botY, 0, 0};   /* Bottom-right */
	GTEVector16 x2 = {halfWidth, topY, 0, 0};   /* Top-right */
	GTEVector16 x3 = {-halfWidth, topY, 0, 0};  /* Top-left */

	gte_loadV0(&x0);
	gte_loadV1(&x1);
	gte_loadV2(&x2);
	gte_command(GTE_CMD_RTPT | GTE_SF);
	gte_command(GTE_CMD_AVSZ3 | GTE_SF);
	zIndex = gte_getDataReg(GTE_OTZ);
	if (zIndex >= 0 && zIndex < ORDERING_TABLE_SIZE) {
		ptr = allocatePacket(chain, zIndex, 4);
		ptr[0] = gp0_rgb(FENCE_COLOR_R, FENCE_COLOR_G, FENCE_COLOR_B) | gp0_triangle(false, false);
		gte_storeDataReg(GTE_SXY0, 1 * 4, ptr);
		gte_storeDataReg(GTE_SXY1, 2 * 4, ptr);
		gte_storeDataReg(GTE_SXY2, 3 * 4, ptr);
		statTriangles++;
	}

	gte_loadV0(&x0);
	gte_loadV1(&x2);
	gte_loadV2(&x3);
	gte_command(GTE_CMD_RTPT | GTE_SF);
	gte_command(GTE_CMD_AVSZ3 | GTE_SF);
	zIndex = gte_getDataReg(GTE_OTZ);
	if (zIndex >= 0 && zIndex < ORDERING_TABLE_SIZE) {
		ptr = allocatePacket(chain, zIndex, 4);
		ptr[0] = gp0_rgb(FENCE_COLOR_R, FENCE_COLOR_G, FENCE_COLOR_B) | gp0_triangle(false, false);
		gte_storeDataReg(GTE_SXY0, 1 * 4, ptr);
		gte_storeDataReg(GTE_SXY1, 2 * 4, ptr);
		gte_storeDataReg(GTE_SXY2, 3 * 4, ptr);
		statTriangles++;
	}

	/* Back face (reversed winding for opposite side visibility) */
	gte_loadV0(&x1);
	gte_loadV1(&x0);
	gte_loadV2(&x3);
	gte_command(GTE_CMD_RTPT | GTE_SF);
	gte_command(GTE_CMD_AVSZ3 | GTE_SF);
	zIndex = gte_getDataReg(GTE_OTZ);
	if (zIndex >= 0 && zIndex < ORDERING_TABLE_SIZE) {
		ptr = allocatePacket(chain, zIndex, 4);
		ptr[0] = gp0_rgb(FENCE_COLOR_R - 15, FENCE_COLOR_G - 10, FENCE_COLOR_B - 5) | gp0_triangle(false, false);
		gte_storeDataReg(GTE_SXY0, 1 * 4, ptr);
		gte_storeDataReg(GTE_SXY1, 2 * 4, ptr);
		gte_storeDataReg(GTE_SXY2, 3 * 4, ptr);
		statTriangles++;
	}

	gte_loadV0(&x1);
	gte_loadV1(&x3);
	gte_loadV2(&x2);
	gte_command(GTE_CMD_RTPT | GTE_SF);
	gte_command(GTE_CMD_AVSZ3 | GTE_SF);
	zIndex = gte_getDataReg(GTE_OTZ);
	if (zIndex >= 0 && zIndex < ORDERING_TABLE_SIZE) {
		ptr = allocatePacket(chain, zIndex, 4);
		ptr[0] = gp0_rgb(FENCE_COLOR_R - 15, FENCE_COLOR_G - 10, FENCE_COLOR_B - 5) | gp0_triangle(false, false);
		gte_storeDataReg(GTE_SXY0, 1 * 4, ptr);
		gte_storeDataReg(GTE_SXY1, 2 * 4, ptr);
		gte_storeDataReg(GTE_SXY2, 3 * 4, ptr);
		statTriangles++;
	}

	/* Wall 2: Z-aligned wall (spans -Z to +Z, at X=0) - double sided */
	/* Front face */
	GTEVector16 z0 = {0, botY, -halfWidth, 0};  /* Bottom-left */
	GTEVector16 z1 = {0, botY, halfWidth, 0};   /* Bottom-right */
	GTEVector16 z2 = {0, topY, halfWidth, 0};   /* Top-right */
	GTEVector16 z3 = {0, topY, -halfWidth, 0};  /* Top-left */

	gte_loadV0(&z0);
	gte_loadV1(&z1);
	gte_loadV2(&z2);
	gte_command(GTE_CMD_RTPT | GTE_SF);
	gte_command(GTE_CMD_AVSZ3 | GTE_SF);
	zIndex = gte_getDataReg(GTE_OTZ);
	if (zIndex >= 0 && zIndex < ORDERING_TABLE_SIZE) {
		ptr = allocatePacket(chain, zIndex, 4);
		ptr[0] = gp0_rgb(FENCE_COLOR_R - 10, FENCE_COLOR_G - 8, FENCE_COLOR_B - 5) | gp0_triangle(false, false);
		gte_storeDataReg(GTE_SXY0, 1 * 4, ptr);
		gte_storeDataReg(GTE_SXY1, 2 * 4, ptr);
		gte_storeDataReg(GTE_SXY2, 3 * 4, ptr);
		statTriangles++;
	}

	gte_loadV0(&z0);
	gte_loadV1(&z2);
	gte_loadV2(&z3);
	gte_command(GTE_CMD_RTPT | GTE_SF);
	gte_command(GTE_CMD_AVSZ3 | GTE_SF);
	zIndex = gte_getDataReg(GTE_OTZ);
	if (zIndex >= 0 && zIndex < ORDERING_TABLE_SIZE) {
		ptr = allocatePacket(chain, zIndex, 4);
		ptr[0] = gp0_rgb(FENCE_COLOR_R - 10, FENCE_COLOR_G - 8, FENCE_COLOR_B - 5) | gp0_triangle(false, false);
		gte_storeDataReg(GTE_SXY0, 1 * 4, ptr);
		gte_storeDataReg(GTE_SXY1, 2 * 4, ptr);
		gte_storeDataReg(GTE_SXY2, 3 * 4, ptr);
		statTriangles++;
	}

	/* Back face (reversed winding) */
	gte_loadV0(&z1);
	gte_loadV1(&z0);
	gte_loadV2(&z3);
	gte_command(GTE_CMD_RTPT | GTE_SF);
	gte_command(GTE_CMD_AVSZ3 | GTE_SF);
	zIndex = gte_getDataReg(GTE_OTZ);
	if (zIndex >= 0 && zIndex < ORDERING_TABLE_SIZE) {
		ptr = allocatePacket(chain, zIndex, 4);
		ptr[0] = gp0_rgb(FENCE_COLOR_R - 20, FENCE_COLOR_G - 15, FENCE_COLOR_B - 10) | gp0_triangle(false, false);
		gte_storeDataReg(GTE_SXY0, 1 * 4, ptr);
		gte_storeDataReg(GTE_SXY1, 2 * 4, ptr);
		gte_storeDataReg(GTE_SXY2, 3 * 4, ptr);
		statTriangles++;
	}

	gte_loadV0(&z1);
	gte_loadV1(&z3);
	gte_loadV2(&z2);
	gte_command(GTE_CMD_RTPT | GTE_SF);
	gte_command(GTE_CMD_AVSZ3 | GTE_SF);
	zIndex = gte_getDataReg(GTE_OTZ);
	if (zIndex >= 0 && zIndex < ORDERING_TABLE_SIZE) {
		ptr = allocatePacket(chain, zIndex, 4);
		ptr[0] = gp0_rgb(FENCE_COLOR_R - 20, FENCE_COLOR_G - 15, FENCE_COLOR_B - 10) | gp0_triangle(false, false);
		gte_storeDataReg(GTE_SXY0, 1 * 4, ptr);
		gte_storeDataReg(GTE_SXY1, 2 * 4, ptr);
		gte_storeDataReg(GTE_SXY2, 3 * 4, ptr);
		statTriangles++;
	}
}

/* Check if player collides with a fence post (box-circle collision) */
static bool checkFencePostCollision(int32_t playerX, int32_t playerZ, int32_t radius,
                                     const FencePost *post) {
	/* Fence post uses MAP_SCALE to match the spacing between fence tile positions */
	int32_t halfTile = MAP_SCALE / 2;

	/* Find closest point on the box to the player */
	int32_t closestX = playerX;
	int32_t closestZ = playerZ;

	if (playerX < post->x - halfTile) closestX = post->x - halfTile;
	else if (playerX > post->x + halfTile) closestX = post->x + halfTile;

	if (playerZ < post->z - halfTile) closestZ = post->z - halfTile;
	else if (playerZ > post->z + halfTile) closestZ = post->z + halfTile;

	/* Check distance from player to closest point */
	int32_t dx = playerX - closestX;
	int32_t dz = playerZ - closestZ;
	int64_t distSq = (int64_t)dx * dx + (int64_t)dz * dz;
	int64_t radiusSq = (int64_t)radius * radius;

	return distSq < radiusSq;
}

/* Check if player collides with a tree (circle-circle collision) */
static bool checkTreeCollision(int32_t playerX, int32_t playerZ, int32_t playerRadius,
                                const Tree *tree) {
	int32_t dx = playerX - tree->x;
	int32_t dz = playerZ - tree->z;
	int32_t combinedRadius = playerRadius + TREE_COLLISION_RADIUS;
	int64_t distSq = (int64_t)dx * dx + (int64_t)dz * dz;
	int64_t radiusSq = (int64_t)combinedRadius * combinedRadius;
	return distSq < radiusSq;
}

/* Check collision against all trees */
static bool checkAllTreeCollisions(int32_t playerX, int32_t playerZ, int32_t radius,
                                    const Tree *trees, int numTrees) {
	for (int i = 0; i < numTrees; i++) {
		if (checkTreeCollision(playerX, playerZ, radius, &trees[i])) {
			return true;
		}
	}
	return false;
}

/* Check collision against all fence posts */
static bool checkAllFenceCollisions(int32_t playerX, int32_t playerZ, int32_t radius) {
	for (int i = 0; i < NUM_FENCE_POSTS; i++) {
		if (checkFencePostCollision(playerX, playerZ, radius, &mapFencePosts[i])) {
			return true;
		}
	}
	return false;
}

/* Controller communication */
static void delayMicroseconds(int time) {
	time = ((time * 271) + 4) / 8;
	__asm__ volatile(
		".set push\n"
		".set noreorder\n"
		"bgtz  %0, .\n"
		"addiu %0, -2\n"
		".set pop\n"
		: "+r"(time)
	);
}

static void initControllerBus(void) {
	SIO_CTRL(0) = SIO_CTRL_RESET;
	SIO_MODE(0) = SIO_MODE_BAUD_DIV1 | SIO_MODE_DATA_8;
	SIO_BAUD(0) = F_CPU / 250000;
	SIO_CTRL(0) = SIO_CTRL_TX_ENABLE | SIO_CTRL_RX_ENABLE | SIO_CTRL_DSR_IRQ_ENABLE;
}

static bool waitForAcknowledge(int timeout) {
	for (; timeout > 0; timeout -= 10) {
		if (IRQ_STAT & (1 << IRQ_SIO0)) {
			IRQ_STAT = ~(1 << IRQ_SIO0);
			SIO_CTRL(0) |= SIO_CTRL_ACKNOWLEDGE;
			return true;
		}
		delayMicroseconds(10);
	}
	return false;
}

static uint8_t exchangeByteWithTimeout(uint8_t value, int timeout) {
	while (!(SIO_STAT(0) & SIO_STAT_TX_NOT_FULL)) {
		if (--timeout <= 0) return 0xFF;
		__asm__ volatile("");
	}
	SIO_DATA(0) = value;

	timeout = 10000;
	while (!(SIO_STAT(0) & SIO_STAT_RX_NOT_EMPTY)) {
		if (--timeout <= 0) return 0xFF;
		__asm__ volatile("");
	}
	return SIO_DATA(0);
}

/* Controller state structure with analog support */
typedef struct {
	uint16_t buttons;
	uint8_t  leftX;
	uint8_t  leftY;
	uint8_t  rightX;
	uint8_t  rightY;
	bool     isAnalog;
} ControllerState;

static void pollController(int port, ControllerState *state) {
	state->buttons = 0;
	state->leftX = 0x80;
	state->leftY = 0x80;
	state->rightX = 0x80;
	state->rightY = 0x80;
	state->isAnalog = false;

	if (port)
		SIO_CTRL(0) |= SIO_CTRL_CS_PORT_2;
	else
		SIO_CTRL(0) &= ~SIO_CTRL_CS_PORT_2;

	IRQ_STAT = ~(1 << IRQ_SIO0);
	SIO_CTRL(0) |= SIO_CTRL_DTR | SIO_CTRL_ACKNOWLEDGE;
	delayMicroseconds(60);

	SIO_DATA(0) = 0x01;

	if (!waitForAcknowledge(500)) {
		SIO_CTRL(0) &= ~SIO_CTRL_DTR;
		return;
	}

	int clearTimeout = 2000;
	while ((SIO_STAT(0) & SIO_STAT_RX_NOT_EMPTY) && clearTimeout-- > 0)
		SIO_DATA(0);

	uint8_t response[8] = {0, 0, 0, 0, 0x80, 0x80, 0x80, 0x80};
	uint8_t request[] = { 0x42, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

	response[0] = exchangeByteWithTimeout(request[0], 20000);
	if (!waitForAcknowledge(500)) goto done;

	int type = response[0] >> 4;
	int halfwords = response[0] & 0x0F;
	int responseLen = (halfwords + 1) * 2;
	if (responseLen > 8) responseLen = 8;

	for (int i = 1; i < responseLen; i++) {
		response[i] = exchangeByteWithTimeout(request[i], 20000);
		if (i < responseLen - 1 && !waitForAcknowledge(500))
			break;
	}

	state->buttons = (response[2] | (response[3] << 8)) ^ 0xFFFF;

	if (type == 0x7 || type == 0x5) {
		state->isAnalog = true;
		state->rightX = response[4];
		state->rightY = response[5];
		state->leftX = response[6];
		state->leftY = response[7];
	}

done:
	delayMicroseconds(60);
	SIO_CTRL(0) &= ~SIO_CTRL_DTR;
}

int main(int argc, const char **argv) {
	/* Initialize serial for debugging */
	initSerialIO(115200);

	/* Initialize controller bus */
	initControllerBus();

	/* Setup GPU based on region */
	if ((GPU_GP1 & GP1_STAT_FB_MODE_BITMASK) == GP1_STAT_FB_MODE_PAL) {
		puts("Using PAL mode");
		setupGPU(GP1_MODE_PAL, SCREEN_WIDTH, SCREEN_HEIGHT);
	} else {
		puts("Using NTSC mode");
		setupGPU(GP1_MODE_NTSC, SCREEN_WIDTH, SCREEN_HEIGHT);
	}

	/* Initialize GTE */
	setupGTE(SCREEN_WIDTH, SCREEN_HEIGHT);

	/* Initialize timer for performance measurement */
	setupTimer();

	/* Enable DMA channels */
	DMA_DPCR |= 0
	| DMA_DPCR_CH_ENABLE(DMA_GPU)
	| DMA_DPCR_CH_ENABLE(DMA_OTC);

	GPU_GP1 = gp1_dmaRequestMode(GP1_DREQ_GP0_WRITE);
	GPU_GP1 = gp1_dispBlank(false);

	/* Upload font to VRAM
	 * Place at Y=256 to be in a separate texture page from any potential
	 * VRAM conflicts at Y=0. This matches the working layout from old-code. */
	TextureInfo font;
	uploadIndexedTexture(
		&font,
		fontTexture,
		fontPalette,
		SCREEN_WIDTH * 2,          /* Image X = 640 */
		256,                       /* Image Y = 256 (texture page Y=1) */
		SCREEN_WIDTH * 2,          /* Palette X = 640 */
		256 + FONT_HEIGHT,         /* Palette Y = 312 (below font image) */
		FONT_WIDTH,
		FONT_HEIGHT,
		FONT_COLOR_DEPTH
	);
	puts("Font uploaded to VRAM");

	/* Initialize SPU */
	setupSPU();
	puts("SPU initialized");

	/* Initialize BIOS events for HLE compatibility */
	biosInit();
	puts("BIOS events initialized");

	/* Upload SPU sound effect */
	uint32_t spuSoundAddr = 0;
	if (musicData_size > 0) {
		spuSoundAddr = uploadVAG(musicData, musicData_size);
		printf("SPU: Sound uploaded to 0x%05lX\n", (unsigned long)spuSoundAddr);
	}

	/* Initialize CD-DA for background music */
	// initCDDA();
	// puts("CD-DA initialized - music playing from disc");

	/* Unmute SPU */
	spuUnmute();
	puts("SPU unmuted - press X for sound effect");

	/* Initialize character */
	Character player;
	if (!initCharacter(&player,
		charBodyData, charBodyData_size,
		charHeadData, charHeadData_size,
		charArmLeftData, charArmLeftData_size,
		charArmRightData, charArmRightData_size,
		charLegLeftData, charLegLeftData_size,
		charLegRightData, charLegRightData_size))
	{
		puts("Failed to initialize character!");
		return 1;
	}
	puts("Character initialized!");

	/* Load house model templates (3 types) */
	Model houseModels[3];
	if (!loadCharacterModel(&houseModels[0], house1Data, house1Data_size)) {
		puts("Failed to load house model 1!");
		return 1;
	}
	if (!loadCharacterModel(&houseModels[1], house2Data, house2Data_size)) {
		puts("Failed to load house model 2!");
		return 1;
	}
	if (!loadCharacterModel(&houseModels[2], house3Data, house3Data_size)) {
		puts("Failed to load house model 3!");
		return 1;
	}
	printf("House models loaded: %d, %d, %d faces\n",
		houseModels[0].numFaces,
		houseModels[1].numFaces,
		houseModels[2].numFaces);

	/* Load house interior models (3 types) */
	Model interiorModels[3];
	if (!loadCharacterModel(&interiorModels[0], house1IntData, house1IntData_size)) {
		puts("Failed to load house 1 interior!");
		return 1;
	}
	if (!loadCharacterModel(&interiorModels[1], house2IntData, house2IntData_size)) {
		puts("Failed to load house 2 interior!");
		return 1;
	}
	if (!loadCharacterModel(&interiorModels[2], house3IntData, house3IntData_size)) {
		puts("Failed to load house 3 interior!");
		return 1;
	}
	printf("Interiors loaded: %d, %d, %d faces\n",
		interiorModels[0].numFaces,
		interiorModels[1].numFaces,
		interiorModels[2].numFaces);

	/* Initialize houses from map data */
	House houses[NUM_HOUSES];
	for (int i = 0; i < NUM_HOUSES; i++) {
		const HouseSpawn *spawn = &mapHouses[i];
		int modelType = spawn->modelType % 3;  /* Ensure valid model type */

		/* Copy model from template */
		houses[i].model = houseModels[modelType];
		houses[i].x = spawn->x;
		houses[i].y = FLOOR_Y;
		houses[i].z = spawn->z;
		houses[i].rotation = spawn->rotation;

		/* Set collision box (same for all house types) */
		houses[i].numCollisionBoxes = 1;
		houses[i].collisionBoxes[0].minX = -HOUSE_COLLISION_SIZE;
		houses[i].collisionBoxes[0].minZ = -HOUSE_COLLISION_SIZE;
		houses[i].collisionBoxes[0].maxX = HOUSE_COLLISION_SIZE;
		houses[i].collisionBoxes[0].maxZ = HOUSE_COLLISION_SIZE;

		/* Set door trigger based on house type */
		switch (modelType) {
			case 0:
				houses[i].door.offsetX = HOUSE1_DOOR_OFFSET_X;
				houses[i].door.offsetZ = HOUSE1_DOOR_OFFSET_Z;
				houses[i].door.sizeX = HOUSE1_DOOR_SIZE_X;
				houses[i].door.sizeZ = HOUSE1_DOOR_SIZE_Z;
				break;
			case 1:
				houses[i].door.offsetX = HOUSE2_DOOR_OFFSET_X;
				houses[i].door.offsetZ = HOUSE2_DOOR_OFFSET_Z;
				houses[i].door.sizeX = HOUSE2_DOOR_SIZE_X;
				houses[i].door.sizeZ = HOUSE2_DOOR_SIZE_Z;
				break;
			case 2:
			default:
				houses[i].door.offsetX = HOUSE3_DOOR_OFFSET_X;
				houses[i].door.offsetZ = HOUSE3_DOOR_OFFSET_Z;
				houses[i].door.sizeX = HOUSE3_DOOR_SIZE_X;
				houses[i].door.sizeZ = HOUSE3_DOOR_SIZE_Z;
				break;
		}
	}
	printf("Initialized %d houses from map data\n", NUM_HOUSES);

	/* Load tree models (2 variants) */
	Model treeModels[2];
	if (!loadCharacterModel(&treeModels[0], treeLargeData, treeLargeData_size)) {
		puts("Failed to load large tree model!");
		return 1;
	}
	if (!loadCharacterModel(&treeModels[1], treeSmallData, treeSmallData_size)) {
		puts("Failed to load small tree model!");
		return 1;
	}
	printf("Tree models loaded: %d, %d faces\n",
		treeModels[0].numFaces,
		treeModels[1].numFaces);

	/* Initialize trees from map data */
	Tree trees[NUM_MAP_TREES];
	for (int i = 0; i < NUM_MAP_TREES; i++) {
		const TreeSpawn *spawn = &mapTrees[i];
		int variant = spawn->variant % 2;

		trees[i].model = treeModels[variant];
		trees[i].x = spawn->x;
		trees[i].y = FLOOR_Y;
		trees[i].z = spawn->z;
	}
	printf("Initialized %d trees from map data\n", NUM_MAP_TREES);

	/* Set player spawn position from map data (convert to 20.12 fixed point) */
	player.x = PLAYER_SPAWN_X << 12;
	player.z = PLAYER_SPAWN_Z << 12;
	printf("Player spawn: %d, %d\n", PLAYER_SPAWN_X, PLAYER_SPAWN_Z);

	/* Double buffering */
	DMAChain dmaChains[2];
	bool     usingSecondFrame = false;

	/* Camera setup */
	Camera cam;
	cameraInit(&cam, 0, -CAMERA_Y_OFFSET, -CAMERA_DISTANCE);
	int16_t orbitAngle = 0;  /* Orbit angle around character */

	/* Track previous button state for edge detection */
	uint16_t prevButtons = 0;

	/* Background flash effect */
	int bgFlash = 0;

	/* Scene state management */
	GameState gameState = STATE_EXTERIOR;
	int currentHouseIndex = -1;        /* Which house we're inside (-1 = none) */
	int fadeAlpha = 0;                 /* Current fade level (0-255) */
	int fadeHoldCounter = 0;           /* Frames to hold on black */
	int32_t entryPosX = 0;             /* Position when entering house */
	int32_t entryPosZ = 0;
	int16_t entryFacing = 0;           /* Facing when entering house */
	bool transitionToInterior = false; /* True = fading to interior, false = to exterior */

	/* Delta time - fixed at 256 (1.0) since PS1 runs at fixed 60fps */
	const int deltaTime = 256;

	puts("Character demo starting...");
	puts("Use D-pad or left stick to walk");
	puts("Press X for sound effect");

	/* Main loop */
	for (;;) {
		int bufferX = usingSecondFrame ? SCREEN_WIDTH : 0;
		int bufferY = 0;

		DMAChain *chain  = &dmaChains[usingSecondFrame];
		usingSecondFrame = !usingSecondFrame;

		uint32_t *ptr;

		GPU_GP1 = gp1_fbOffset(bufferX, bufferY);

		clearOrderingTable(chain->orderingTable, ORDERING_TABLE_SIZE);
		chain->nextPacket = chain->data;

		/* Reset stats */
		statTriangles = 0;
		statTiles = 0;

		/* Record frame start for profiling */
		TIMER_VALUE(2) = 0;
		uint16_t frameStart = TIMER_VALUE(2);

		/* Poll controller (use uint16_t for timer to handle wraparound) */
		uint16_t t0 = TIMER_VALUE(2);
		ControllerState pad;
		pollController(0, &pad);
		uint16_t t1 = TIMER_VALUE(2);
		statPadTime = (uint16_t)(t1 - t0);  /* uint16_t subtraction handles wrap */

		/* Get movement input (disabled during fade transitions) */
		int16_t moveX = 0;
		int16_t moveZ = 0;
		bool strafeMode = false;
		int32_t strafeDirX = 0;
		int32_t strafeDirZ = 0;

		/* Only process input when not fading */
		bool canProcessInput = (gameState == STATE_EXTERIOR || gameState == STATE_INTERIOR);

		/* Check if L2 is held for strafe mode - only when input allowed */
		if (canProcessInput) {
			strafeMode = (pad.buttons & PAD_L2) != 0;

			/* Force strafe mode when inside a house */
			if (gameState == STATE_INTERIOR) {
				strafeMode = true;
			}

			if (strafeMode) {
				/* Strafe mode: move relative to camera, character turns to face movement */
				/* Interior uses fixed camera angle so movement matches screen directions */
				int16_t strafeAngle = orbitAngle;
				if (gameState == STATE_INTERIOR) {
					strafeAngle = INTERIOR_CAMERA_ANGLE;
				}

				if (pad.isAnalog) {
					int stickX = (int)pad.leftX - 0x80;
					int stickY = (int)pad.leftY - 0x80;

					/* Calculate strafe direction from stick + camera angle */
					if (stickY < -ANALOG_DEADZONE) {
						/* Up = backward relative to camera */
						strafeDirX -= isin(strafeAngle);
						strafeDirZ -= icos(strafeAngle);
					} else if (stickY > ANALOG_DEADZONE) {
						/* Down = forward relative to camera */
						strafeDirX += isin(strafeAngle);
						strafeDirZ += icos(strafeAngle);
					}
					if (stickX < -ANALOG_DEADZONE) {
						/* Left = strafe left relative to camera */
						int16_t leftAngle = strafeAngle + 1024;
						strafeDirX += isin(leftAngle);
						strafeDirZ += icos(leftAngle);
					} else if (stickX > ANALOG_DEADZONE) {
						/* Right = strafe right relative to camera */
						int16_t rightAngle = strafeAngle - 1024;
						strafeDirX += isin(rightAngle);
						strafeDirZ += icos(rightAngle);
					}
				}

				/* D-pad strafe input */
				if (pad.buttons & PAD_UP) {
					strafeDirX -= isin(strafeAngle);
					strafeDirZ -= icos(strafeAngle);
				}
				if (pad.buttons & PAD_DOWN) {
					strafeDirX += isin(strafeAngle);
					strafeDirZ += icos(strafeAngle);
				}
				if (pad.buttons & PAD_LEFT) {
					int16_t leftAngle = strafeAngle + 1024;
					strafeDirX += isin(leftAngle);
					strafeDirZ += icos(leftAngle);
				}
				if (pad.buttons & PAD_RIGHT) {
					int16_t rightAngle = strafeAngle - 1024;
					strafeDirX += isin(rightAngle);
					strafeDirZ += icos(rightAngle);
				}

				/* If moving, turn character to face movement direction */
				if (strafeDirX != 0 || strafeDirZ != 0) {
					moveZ = 1;  /* Walk forward */

					/* Calculate target facing from movement direction */
					int16_t targetFacing = iatan2(strafeDirX, strafeDirZ);

					/* Calculate turn direction toward target (with threshold to avoid jitter) */
					int16_t diff = targetFacing - player.facing;
					while (diff > 2048) diff -= 4096;
					while (diff < -2048) diff += 4096;

					if (diff > ROTATION_THRESHOLD) moveX = 1;        /* Turn right */
					else if (diff < -ROTATION_THRESHOLD) moveX = -1; /* Turn left */
				}
			} else {
				/* Normal mode: turn and move relative to character facing */

				/* Analog stick input */
				if (pad.isAnalog) {
					int stickX = (int)pad.leftX - 0x80;
					int stickY = (int)pad.leftY - 0x80;

					/* X axis: turn left/right */
					if (stickX > ANALOG_DEADZONE) moveX = 1;
					else if (stickX < -ANALOG_DEADZONE) moveX = -1;

					/* Y axis: up = forward, down = turn toward camera */
					if (stickY < -ANALOG_DEADZONE) {
						moveZ = 1;  /* Forward */
					} else if (stickY > ANALOG_DEADZONE) {
						/* Smoothly rotate toward camera (with threshold to avoid jitter) */
						int16_t diff = orbitAngle - player.facing;
						while (diff > 2048) diff -= 4096;
						while (diff < -2048) diff += 4096;
						if (diff > ROTATION_THRESHOLD) moveX = 1;
						else if (diff < -ROTATION_THRESHOLD) moveX = -1;
					}
				}

				/* D-pad input (overrides analog if pressed) */
				if (pad.buttons & PAD_LEFT)  moveX = -1;
				if (pad.buttons & PAD_RIGHT) moveX = 1;
				if (pad.buttons & PAD_UP)    moveZ = 1;  /* Forward */
				if (pad.buttons & PAD_DOWN) {
					/* Smoothly rotate toward camera (with threshold to avoid jitter) */
					int16_t diff = orbitAngle - player.facing;
					while (diff > 2048) diff -= 4096;
					while (diff < -2048) diff += 4096;
					if (diff > ROTATION_THRESHOLD) moveX = 1;
					else if (diff < -ROTATION_THRESHOLD) moveX = -1;
				}
			}
		}

		/* Scene transition state machine */
		if (gameState == STATE_FADE_OUT) {
			fadeAlpha += FADE_SPEED;
			if (fadeAlpha >= 255) {
				fadeAlpha = 255;
				/* Switch scene while fully black */
				if (transitionToInterior) {
					/* Center player in interior */
					player.x = 0;
					player.z = 0;
					player.facing = 0;
					player.isWalking = false;
				} else {
					/* Restore player to entry position, facing away from door */
					player.x = entryPosX;
					player.z = entryPosZ;
					player.facing = entryFacing + 2048;  /* 180° turn to face outward */
					player.isWalking = false;
					currentHouseIndex = -1;
				}
				/* Hold on black before fading in */
				gameState = STATE_BLACK;
				fadeHoldCounter = FADE_HOLD_FRAMES;
			}
		} else if (gameState == STATE_BLACK) {
			/* Hold on full black while scene loads/renders */
			fadeAlpha = 255;
			fadeHoldCounter--;
			if (fadeHoldCounter <= 0) {
				gameState = STATE_FADE_IN;
			}
		} else if (gameState == STATE_FADE_IN) {
			fadeAlpha -= FADE_SPEED;
			if (fadeAlpha <= 0) {
				fadeAlpha = 0;
				/* Transition complete - go to final scene */
				if (transitionToInterior) {
					gameState = STATE_INTERIOR;
				} else {
					gameState = STATE_EXTERIOR;
				}
			}
		}

		/* Fade flash */
		if (bgFlash > 0) {
			bgFlash -= BG_FLASH_FADE_SPEED;
			if (bgFlash < 0) bgFlash = 0;
		}

		/* Store old position for collision response */
		int32_t oldX = player.x;
		int32_t oldZ = player.z;

		/* Update character animation and movement */
		updateCharacter(&player, moveX, moveZ, deltaTime);

		/* In strafe mode, override updateCharacter's movement with strafe movement */
		if (strafeMode && (strafeDirX != 0 || strafeDirZ != 0)) {
			/* Reset position (undo updateCharacter's movement) */
			player.x = oldX;
			player.z = oldZ;

			/* Apply strafe movement directly (scaled by deltaTime)
			 * Calculate base movement first, then scale to avoid overflow */
			int32_t strafeBaseX = (strafeDirX * PLAYER_MOVE_SPEED) >> FP_SHIFT;
			int32_t strafeBaseZ = (strafeDirZ * PLAYER_MOVE_SPEED) >> FP_SHIFT;
			player.x += (strafeBaseX * deltaTime) >> 8;
			player.z += (strafeBaseZ * deltaTime) >> 8;
		}

		/* Apply outdoor speed multiplier when in exterior */
		if (gameState == STATE_EXTERIOR) {
			int32_t deltaX = player.x - oldX;
			int32_t deltaZ = player.z - oldZ;
			player.x = oldX + ((deltaX * OUTDOOR_SPEED_MULT) >> 8);
			player.z = oldZ + ((deltaZ * OUTDOOR_SPEED_MULT) >> 8);
		}

		/* Collision handling depends on scene */
		if (gameState == STATE_EXTERIOR) {
			/* Check collision with houses, trees, and fences - handle wall sliding */
			int32_t newWorldX = player.x >> 12;  /* Convert to world units */
			int32_t newWorldZ = player.z >> 12;
			int32_t oldWorldX = oldX >> 12;
			int32_t oldWorldZ = oldZ >> 12;

			/* Check all collision types (trees disabled for debugging) */
			bool hasCollision =
				checkAllHouseCollisions(newWorldX, newWorldZ, PLAYER_COLLISION_RADIUS, houses, NUM_HOUSES) ||
				/* checkAllTreeCollisions(newWorldX, newWorldZ, PLAYER_COLLISION_RADIUS, trees, NUM_MAP_TREES) || */
				checkAllFenceCollisions(newWorldX, newWorldZ, PLAYER_COLLISION_RADIUS);

			if (hasCollision) {
				/* Collision detected - try sliding along walls */
				/* Try moving only in X (keep old Z) */
				bool canMoveX = !(
					checkAllHouseCollisions(newWorldX, oldWorldZ, PLAYER_COLLISION_RADIUS, houses, NUM_HOUSES) ||
					/* checkAllTreeCollisions(newWorldX, oldWorldZ, PLAYER_COLLISION_RADIUS, trees, NUM_MAP_TREES) || */
					checkAllFenceCollisions(newWorldX, oldWorldZ, PLAYER_COLLISION_RADIUS));
				/* Try moving only in Z (keep old X) */
				bool canMoveZ = !(
					checkAllHouseCollisions(oldWorldX, newWorldZ, PLAYER_COLLISION_RADIUS, houses, NUM_HOUSES) ||
					/* checkAllTreeCollisions(oldWorldX, newWorldZ, PLAYER_COLLISION_RADIUS, trees, NUM_MAP_TREES) || */
					checkAllFenceCollisions(oldWorldX, newWorldZ, PLAYER_COLLISION_RADIUS));

				if (canMoveX && !canMoveZ) {
					/* Slide along X axis only */
					player.z = oldZ;
				} else if (canMoveZ && !canMoveX) {
					/* Slide along Z axis only */
					player.x = oldX;
				} else {
					/* Can't move in either direction - fully block */
					player.x = oldX;
					player.z = oldZ;
				}
			}
		} else if (gameState == STATE_INTERIOR) {
			/* Clamp player to interior floor bounds */
			int32_t minX = -INTERIOR_FLOOR_HALF_X << 12;
			int32_t maxX = INTERIOR_FLOOR_HALF_X << 12;
			int32_t minZ = -INTERIOR_FLOOR_HALF_Z << 12;
			int32_t maxZ = INTERIOR_FLOOR_HALF_Z << 12;

			if (player.x < minX) player.x = minX;
			if (player.x > maxX) player.x = maxX;
			if (player.z < minZ) player.z = minZ;
			if (player.z > maxZ) player.z = maxZ;
		}

		/* Check door triggers (use current position after collision response) */
		int triggeredDoor = -1;
		bool atInteriorExit = false;

		if (gameState == STATE_EXTERIOR) {
			triggeredDoor = checkDoorTrigger(player.x >> 12, player.z >> 12,
			                                  houses, NUM_HOUSES);
		} else if (gameState == STATE_INTERIOR && currentHouseIndex >= 0) {
			/* Check if player is at interior exit door using per-house settings */
			int32_t playerLocalX = player.x >> 12;
			int32_t playerLocalZ = player.z >> 12;

			/* Get per-house door settings */
			int32_t doorX, doorZ, doorSizeX, doorSizeZ;
			switch (currentHouseIndex) {
				case 0:
					doorX = HOUSE1_INT_DOOR_X; doorZ = HOUSE1_INT_DOOR_Z;
					doorSizeX = HOUSE1_INT_DOOR_SIZE_X; doorSizeZ = HOUSE1_INT_DOOR_SIZE_Z;
					break;
				case 1:
					doorX = HOUSE2_INT_DOOR_X; doorZ = HOUSE2_INT_DOOR_Z;
					doorSizeX = HOUSE2_INT_DOOR_SIZE_X; doorSizeZ = HOUSE2_INT_DOOR_SIZE_Z;
					break;
				default:
					doorX = HOUSE3_INT_DOOR_X; doorZ = HOUSE3_INT_DOOR_Z;
					doorSizeX = HOUSE3_INT_DOOR_SIZE_X; doorSizeZ = HOUSE3_INT_DOOR_SIZE_Z;
					break;
			}

			/* Check if player is at interior exit door */
			if (playerLocalX >= doorX - doorSizeX && playerLocalX <= doorX + doorSizeX &&
			    playerLocalZ >= doorZ - doorSizeZ && playerLocalZ <= doorZ + doorSizeZ) {
				atInteriorExit = true;
			}
		}

		/* X button handling depends on game state */
		if ((pad.buttons & PAD_X) && !(prevButtons & PAD_X)) {
			if (gameState == STATE_EXTERIOR && triggeredDoor >= 0) {
				/* Start transition to interior */
				entryPosX = player.x;
				entryPosZ = player.z;
				entryFacing = player.facing;
				currentHouseIndex = triggeredDoor;
				transitionToInterior = true;
				gameState = STATE_FADE_OUT;
				fadeAlpha = 0;
			} else if (gameState == STATE_INTERIOR && atInteriorExit) {
				/* Start transition to exterior */
				transitionToInterior = false;
				gameState = STATE_FADE_OUT;
				fadeAlpha = 0;
			}
			/* Sound effect and flash disabled for now
			else if (gameState == STATE_EXTERIOR || gameState == STATE_INTERIOR) {
				// Normal X button action - sound effect and flash
				if (spuSoundAddr != 0) {
					playSample(0, spuSoundAddr, SFX_SAMPLE_RATE, SFX_VOLUME);
				}
				bgFlash = 255;
			}
			*/
		}
		prevButtons = pad.buttons;

		/* Update CD-DA looping */
		// updateCDDA();

		/* Determine which scene to render based on state and transition direction
		 * FADE_OUT: render the scene we're LEAVING (opposite of transition direction)
		 * BLACK/FADE_IN: render the scene we're GOING TO (same as transition direction) */
		bool renderInterior = (gameState == STATE_INTERIOR) ||
		                      (gameState == STATE_FADE_OUT && !transitionToInterior) ||  /* leaving interior */
		                      ((gameState == STATE_BLACK || gameState == STATE_FADE_IN) && transitionToInterior);  /* entering interior */

		/* Camera handling depends on scene being rendered */
		if (!renderInterior) {
			/* Manual camera rotation with L1/R1 bumpers (exterior only) */
			if (gameState == STATE_EXTERIOR) {
				if (pad.buttons & PAD_L1) {
					orbitAngle -= 40;  /* Rotate left */
				}
				if (pad.buttons & PAD_R1) {
					orbitAngle += 40;  /* Rotate right */
				}

				/* Camera follow logic: when player is moving, camera rotates to follow
				 * Moving forward: camera behind player (facing + 2048)
				 * L2 held = strafe mode (camera stays fixed) */
				if (player.isWalking && !strafeMode) {
					int16_t targetOrbit = player.facing + 2048;  /* Camera behind player */

					/* Normalize target to -2048..2047 range */
					while (targetOrbit > 2048) targetOrbit -= 4096;
					while (targetOrbit < -2048) targetOrbit += 4096;

					/* Calculate shortest angular distance */
					int16_t diff = targetOrbit - orbitAngle;

					/* Handle wraparound for shortest path */
					if (diff > 2048) diff -= 4096;
					if (diff < -2048) diff += 4096;

					/* Smoothly interpolate (using camera follow divisor from config) */
					orbitAngle += diff / CAMERA_FOLLOW_DIVISOR;
				}
			}

			/* Keep orbit angle in valid range */
			while (orbitAngle > 2048) orbitAngle -= 4096;
			while (orbitAngle < -2048) orbitAngle += 4096;

			/* Get player position in world units */
			int32_t playerWorldX = player.x >> 12;
			int32_t playerWorldY = player.y >> 12;
			int32_t playerWorldZ = player.z >> 12;

			/* Update camera to orbit around player */
			cameraOrbit(&cam, playerWorldX, playerWorldY, playerWorldZ,
			            orbitAngle, CAMERA_DISTANCE, -CAMERA_Y_OFFSET);
		} else {
			/* Fixed camera for interior - looking at room center from fixed angle */
			/* Get per-house-type camera settings based on house's model type */
			int32_t interiorCamDist = INTERIOR_CAMERA_DISTANCE;
			int32_t interiorCamY = INTERIOR_CAMERA_Y_OFFSET;
			if (currentHouseIndex >= 0 && currentHouseIndex < NUM_HOUSES) {
				int modelType = mapHouses[currentHouseIndex].modelType % 3;
				switch (modelType) {
					case 0: interiorCamDist = HOUSE1_INT_CAM_DIST; interiorCamY = HOUSE1_INT_CAM_Y; break;
					case 1: interiorCamDist = HOUSE2_INT_CAM_DIST; interiorCamY = HOUSE2_INT_CAM_Y; break;
					case 2: interiorCamDist = HOUSE3_INT_CAM_DIST; interiorCamY = HOUSE3_INT_CAM_Y; break;
				}
			}
			/* Camera orbits around room center (0,0,0) at fixed angle */
			cameraOrbit(&cam, 0, FLOOR_Y, 0,
			            INTERIOR_CAMERA_ANGLE, interiorCamDist, -interiorCamY);
		}

		/* Draw order: floor first (background), then all models together
		 * This helps the ordering table sort models correctly against each other */

		uint16_t t2 = TIMER_VALUE(2);
		uint16_t t3, t4;

		/* Background colors depend on scene */
		int topR, topG, topB, botR, botG, botB;

		if (renderInterior) {
			/* Interior scene rendering - house model has its own floor */
			t3 = TIMER_VALUE(2);
			statFloorTime = 0;  /* No separate floor to draw */

			/* Draw the current house interior (model centered at origin) */
			if (currentHouseIndex >= 0 && currentHouseIndex < NUM_HOUSES) {
				/* Get per-house-type interior settings based on house's model type */
				int modelType = mapHouses[currentHouseIndex].modelType % 3;
				int16_t interiorRotation = 0;
				int32_t modelOffsetX = 0, modelOffsetZ = 0;
				int32_t doorX = 0, doorZ = 0, doorSizeX = 0, doorSizeZ = 0;
				switch (modelType) {
					case 0:
						interiorRotation = HOUSE1_INT_ROTATION;
						modelOffsetX = HOUSE1_INT_MODEL_X; modelOffsetZ = HOUSE1_INT_MODEL_Z;
						doorX = HOUSE1_INT_DOOR_X; doorZ = HOUSE1_INT_DOOR_Z;
						doorSizeX = HOUSE1_INT_DOOR_SIZE_X; doorSizeZ = HOUSE1_INT_DOOR_SIZE_Z;
						break;
					case 1:
						interiorRotation = HOUSE2_INT_ROTATION;
						modelOffsetX = HOUSE2_INT_MODEL_X; modelOffsetZ = HOUSE2_INT_MODEL_Z;
						doorX = HOUSE2_INT_DOOR_X; doorZ = HOUSE2_INT_DOOR_Z;
						doorSizeX = HOUSE2_INT_DOOR_SIZE_X; doorSizeZ = HOUSE2_INT_DOOR_SIZE_Z;
						break;
					default:
						interiorRotation = HOUSE3_INT_ROTATION;
						modelOffsetX = HOUSE3_INT_MODEL_X; modelOffsetZ = HOUSE3_INT_MODEL_Z;
						doorX = HOUSE3_INT_DOOR_X; doorZ = HOUSE3_INT_DOOR_Z;
						doorSizeX = HOUSE3_INT_DOOR_SIZE_X; doorSizeZ = HOUSE3_INT_DOOR_SIZE_Z;
						break;
				}

				/* Create temporary house struct with interior model */
				House interiorHouse;
				interiorHouse.model = interiorModels[modelType];
				interiorHouse.x = modelOffsetX;
				interiorHouse.y = FLOOR_Y;
				interiorHouse.z = modelOffsetZ;
				interiorHouse.rotation = interiorRotation;
				drawHouse(chain, &interiorHouse, &cam);

#if DEBUG_DRAW_COLLISION
				/* Draw interior debug: floor bounds and door trigger */
				drawInteriorDebug(chain, &cam, doorX, doorZ, doorSizeX, doorSizeZ);
#endif
			}

			/* Draw character in interior */
			drawCharacter(chain, &player, &cam);

			t4 = TIMER_VALUE(2);
			statCharTime = (uint16_t)(t4 - t3);

			/* Interior background colors */
			topR = INTERIOR_BG_TOP_R + ((BG_FLASH_TOP_R - INTERIOR_BG_TOP_R) * bgFlash) / 255;
			topG = INTERIOR_BG_TOP_G + ((BG_FLASH_TOP_G - INTERIOR_BG_TOP_G) * bgFlash) / 255;
			topB = INTERIOR_BG_TOP_B + ((BG_FLASH_TOP_B - INTERIOR_BG_TOP_B) * bgFlash) / 255;
			botR = INTERIOR_BG_BOT_R + ((BG_FLASH_BOT_R - INTERIOR_BG_BOT_R) * bgFlash) / 255;
			botG = INTERIOR_BG_BOT_G + ((BG_FLASH_BOT_G - INTERIOR_BG_BOT_G) * bgFlash) / 255;
			botB = INTERIOR_BG_BOT_B + ((BG_FLASH_BOT_B - INTERIOR_BG_BOT_B) * bgFlash) / 255;
		} else {
			/* Exterior scene rendering */
			/* 1. Draw floor tiles (background layer) */
			drawFloor(chain, &cam);
			t3 = TIMER_VALUE(2);
			statFloorTime = (uint16_t)(t3 - t2);

			/* 2. Draw all models (they sort among themselves via ordering table) */
			drawCharacter(chain, &player, &cam);
			for (int i = 0; i < NUM_HOUSES; i++) {
				drawHouse(chain, &houses[i], &cam);
			}

			/* 3. Draw trees (disabled for debugging) */
			/*setupTreeBatch(&cam);
			for (int i = 0; i < NUM_MAP_TREES; i++) {
				drawTree(chain, &trees[i], &cam);
			}*/

			/* 4. Draw fence posts (batched - set up GTE once) */
			setupFenceBatch(&cam);
			for (int i = 0; i < NUM_FENCE_POSTS; i++) {
				drawFencePost(chain, &mapFencePosts[i], &cam);
			}

#if DEBUG_DRAW_COLLISION
			/* 5. Draw collision debug wireframes */
			drawAllCollisionDebug(chain, houses, NUM_HOUSES, &cam);
			/* 6. Draw door trigger wireframes (red) */
			drawAllDoorTriggersDebug(chain, houses, NUM_HOUSES, &cam);
#endif

			t4 = TIMER_VALUE(2);
			statCharTime = (uint16_t)(t4 - t3);

			/* Exterior background colors */
			topR = BG_TOP_R + ((BG_FLASH_TOP_R - BG_TOP_R) * bgFlash) / 255;
			topG = BG_TOP_G + ((BG_FLASH_TOP_G - BG_TOP_G) * bgFlash) / 255;
			topB = BG_TOP_B + ((BG_FLASH_TOP_B - BG_TOP_B) * bgFlash) / 255;
			botR = BG_BOT_R + ((BG_FLASH_BOT_R - BG_BOT_R) * bgFlash) / 255;
			botG = BG_BOT_G + ((BG_FLASH_BOT_G - BG_BOT_G) * bgFlash) / 255;
			botB = BG_BOT_B + ((BG_FLASH_BOT_B - BG_BOT_B) * bgFlash) / 255;
		}

		/* Draw gradient background as two Gouraud-shaded triangles */
		ptr = allocatePacket(chain, ORDERING_TABLE_SIZE - 1, 6);
		ptr[0] = gp0_rgb(topR, topG, topB) | gp0_shadedTriangle(true, false, false);
		ptr[1] = gp0_xy(0, 0);
		ptr[2] = gp0_rgb(topR, topG, topB);
		ptr[3] = gp0_xy(SCREEN_WIDTH, 0);
		ptr[4] = gp0_rgb(botR, botG, botB);
		ptr[5] = gp0_xy(SCREEN_WIDTH, SCREEN_HEIGHT);

		ptr = allocatePacket(chain, ORDERING_TABLE_SIZE - 1, 6);
		ptr[0] = gp0_rgb(topR, topG, topB) | gp0_shadedTriangle(true, false, false);
		ptr[1] = gp0_xy(0, 0);
		ptr[2] = gp0_rgb(botR, botG, botB);
		ptr[3] = gp0_xy(SCREEN_WIDTH, SCREEN_HEIGHT);
		ptr[4] = gp0_rgb(botR, botG, botB);
		ptr[5] = gp0_xy(0, SCREEN_HEIGHT);

		/* Measure CPU time before GPU wait */
		uint16_t cpuEnd = TIMER_VALUE(2);

		/* Calculate current frame's CPU time */
		uint16_t currentFrameTime = (uint16_t)(cpuEnd - frameStart);

		/* Smooth the frame time using exponential moving average (7/8 old + 1/8 new)
		 * This reduces jitter in the displayed stats */
		statFrameTime = (statFrameTime * 7 + currentFrameTime) / 8;

		/* Calculate FPS and CPU percentage from smoothed time
		 * Timer runs at CPU/8 = 33.8MHz/8 = 4.225MHz
		 * 60fps frame budget = 4225000/60 = 70416 ticks */
		int fps = (statFrameTime > 0) ? (4225000 / statFrameTime) : 60;
		if (fps > 99) fps = 99;
		int cpuPercent = ((long)statFrameTime * 100) / 70416;
		if (cpuPercent > 99) cpuPercent = 99;

		/* Build visual CPU bar: 10 chars wide */
		char cpuBar[12];
		int filled = (cpuPercent + 5) / 10;  /* Round to nearest 10% */
		for (int i = 0; i < 10; i++) {
			cpuBar[i] = (i < filled) ? '#' : '-';
		}
		cpuBar[10] = '\0';

#if DEBUG_UI
		/* Convert times to percentages of frame budget (use long to avoid overflow) */
		int padPct = ((long)statPadTime * 100) / 70416;
		int floorPct = ((long)statFloorTime * 100) / 70416;
		int charPct = ((long)statCharTime * 100) / 70416;

		/* Display performance stats */
		char debugText[128];
		sprintf(debugText, "FPS:%2d Tri:%3d [%s]%2d%%\nPad:%2d%% Floor:%2d%% Char:%2d%%",
		        fps, statTriangles, cpuBar, cpuPercent,
		        padPct, floorPct, charPct);
		printString(chain, &font, 8, 8, debugText);
#endif

		/* Display door prompt when player is near a door */
		if (gameState == STATE_EXTERIOR && triggeredDoor >= 0) {
			/* Get house address */
			uint16_t houseAddr = mapHouses[triggeredDoor].address;
			char addrText[20];
			sprintf(addrText, "House %d", houseAddr);

			const char *doorPrompt = "press [X] to enter";
			int promptX = 110;  /* Roughly centered for this string */
			int promptY = 190; /* Near bottom of screen */

			/* Draw house address first (above prompt) */
			int addrX = 125;  /* Roughly centered */
			printStringColor(chain, &font, addrX + 1, promptY - 9, addrText, 20, 20, 40);
			printStringColor(chain, &font, addrX, promptY - 10, addrText, 255, 220, 100);

			/* Draw drop shadow first (offset by 1 pixel) */
			printStringColor(chain, &font, promptX + 1, promptY + 1, doorPrompt, 20, 20, 40);
			/* Draw blue text on top */
			printStringColor(chain, &font, promptX, promptY, doorPrompt, 100, 150, 255);
		} else if (gameState == STATE_INTERIOR && atInteriorExit) {
			const char *doorPrompt = "press [X] to leave";
			int promptX = 110;  /* Roughly centered */
			int promptY = 200;
			printStringColor(chain, &font, promptX + 1, promptY + 1, doorPrompt, 20, 20, 40);
			printStringColor(chain, &font, promptX, promptY, doorPrompt, 100, 150, 255);
		}

		/* Draw fade overlay if fading */
		if (fadeAlpha > 0) {
			/* Use average blending (mode 0): Result = 0.5*Back + 0.5*Front
			 * With BLACK quads (0,0,0): Result = 0.5*Back (halves brightness)
			 * Multiple black quads stack: 1=50%, 2=25%, 3=12.5%, 4=6.25%
			 * Texpage bits 5-6 = blend mode, mode 0 = 0x00 */
			ptr = allocatePacket(chain, 0, 1);
			ptr[0] = gp0_texpage(0x00, false, false);  /* Blend mode 0 (average) */

			/* Calculate how many black quads to draw based on fadeAlpha
			 * fadeAlpha 0: 0 quads, fadeAlpha 255: 4 quads (near black) */
			int numQuads = (fadeAlpha * 4) / 255;
			if (numQuads < 1) numQuads = 1;
			if (numQuads > 4) numQuads = 4;

			/* Draw black triangles (two per quad for full screen coverage) */
			for (int q = 0; q < numQuads; q++) {
				ptr = allocatePacket(chain, 0, 4);
				ptr[0] = gp0_rgb(0, 0, 0) | gp0_triangle(false, true);
				ptr[1] = gp0_xy(0, 0);
				ptr[2] = gp0_xy(SCREEN_WIDTH, 0);
				ptr[3] = gp0_xy(SCREEN_WIDTH, SCREEN_HEIGHT);

				ptr = allocatePacket(chain, 0, 4);
				ptr[0] = gp0_rgb(0, 0, 0) | gp0_triangle(false, true);
				ptr[1] = gp0_xy(0, 0);
				ptr[2] = gp0_xy(SCREEN_WIDTH, SCREEN_HEIGHT);
				ptr[3] = gp0_xy(0, SCREEN_HEIGHT);
			}
		}

		/* Set drawing area attributes */
		ptr    = allocatePacket(chain, ORDERING_TABLE_SIZE - 1, 4);
		ptr[0] = gp0_texpage(0, true, false);
		ptr[1] = gp0_fbOffset1(bufferX, bufferY);
		ptr[2] = gp0_fbOffset2(
			bufferX + SCREEN_WIDTH  - 1,
			bufferY + SCREEN_HEIGHT - 2
		);
		ptr[3] = gp0_fbOrigin(bufferX, bufferY);

		/* Wait for GPU and VSync, then draw */
		uint16_t gpuStart = TIMER_VALUE(2);
		waitForGP0Ready();
		uint16_t gpuEnd = TIMER_VALUE(2);
		statGpuWait = (uint16_t)(gpuEnd - gpuStart);

		waitForVSync();
		sendLinkedList(&(chain->orderingTable)[ORDERING_TABLE_SIZE - 1]);
	}

	return 0;
}
