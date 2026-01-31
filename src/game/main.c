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

/* Number of houses in the world */
#define NUM_HOUSES 3

/* Maximum collision boxes per house (for concave shapes) */
#define MAX_COLLISION_BOXES 4

/* Axis-Aligned Bounding Box for collision (in local space, relative to house center) */
typedef struct {
	int32_t minX, minZ;  /* Min corner (world units) */
	int32_t maxX, maxZ;  /* Max corner (world units) */
} CollisionBox;

/* House structure - static world object */
typedef struct {
	Model model;
	int32_t x, y, z;  /* World position */
	int16_t rotation; /* Y rotation (0-4095 = 0-360 degrees) */

	/* Collision data */
	int numCollisionBoxes;
	CollisionBox collisionBoxes[MAX_COLLISION_BOXES];
} House;

/* Player collision radius (world units) */
#define PLAYER_COLLISION_RADIUS 40

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
 * House position is in world coordinates, boxes are in local space */
static bool checkHouseCollision(int32_t playerX, int32_t playerZ, int32_t radius,
                                 const House *house) {
	/* Check each collision box */
	for (int i = 0; i < house->numCollisionBoxes; i++) {
		const CollisionBox *box = &house->collisionBoxes[i];

		/* Transform box to world space (add house position) */
		int32_t worldMinX = house->x + box->minX;
		int32_t worldMinZ = house->z + box->minZ;
		int32_t worldMaxX = house->x + box->maxX;
		int32_t worldMaxZ = house->z + box->maxZ;

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

	/* Set projection plane distance (FOV control) */
	int focalLength = (width < height) ? width : height;
	gte_setControlReg(GTE_H, focalLength / 2);

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

			/* Use noise to select grass color variant */
			int noise = tileNoise(tileX, tileZ);
			uint8_t r, g, b;
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

	/* Initialize houses */
	House houses[NUM_HOUSES];

	/* Load house models (using character model loader for vertex colors) */
	if (!loadCharacterModel(&houses[0].model, house1Data, house1Data_size)) {
		puts("Failed to load house 1!");
		return 1;
	}
	if (!loadCharacterModel(&houses[1].model, house2Data, house2Data_size)) {
		puts("Failed to load house 2!");
		return 1;
	}
	if (!loadCharacterModel(&houses[2].model, house3Data, house3Data_size)) {
		puts("Failed to load house 3!");
		return 1;
	}

	/* Position houses around the player in a triangle pattern */
	/* House 1: front-left */
	houses[0].x = -800;
	houses[0].y = FLOOR_Y;
	houses[0].z = 1000;
	houses[0].rotation = 512;  /* Facing slightly right */
	/* Collision box for house 1 (single box for simple hut) */
	houses[0].numCollisionBoxes = 1;
	houses[0].collisionBoxes[0].minX = -120;
	houses[0].collisionBoxes[0].minZ = -120;
	houses[0].collisionBoxes[0].maxX = 120;
	houses[0].collisionBoxes[0].maxZ = 120;

	/* House 2: front-right */
	houses[1].x = 800;
	houses[1].y = FLOOR_Y;
	houses[1].z = 1200;
	houses[1].rotation = -512;  /* Facing slightly left */
	/* Collision box for house 2 */
	houses[1].numCollisionBoxes = 1;
	houses[1].collisionBoxes[0].minX = -120;
	houses[1].collisionBoxes[0].minZ = -120;
	houses[1].collisionBoxes[0].maxX = 120;
	houses[1].collisionBoxes[0].maxZ = 120;

	/* House 3: behind player */
	houses[2].x = 0;
	houses[2].y = FLOOR_Y;
	houses[2].z = -1000;
	houses[2].rotation = 2048;  /* Facing player spawn */
	/* Collision box for house 3 */
	houses[2].numCollisionBoxes = 1;
	houses[2].collisionBoxes[0].minX = -120;
	houses[2].collisionBoxes[0].minZ = -120;
	houses[2].collisionBoxes[0].maxX = 120;
	houses[2].collisionBoxes[0].maxZ = 120;

	printf("Houses loaded: %d, %d, %d faces\n",
		houses[0].model.numFaces,
		houses[1].model.numFaces,
		houses[2].model.numFaces);

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

		/* Reset stats and start frame timer */
		statTriangles = 0;
		statTiles = 0;
		TIMER_VALUE(2) = 0;
		uint16_t frameStart = TIMER_VALUE(2);

		/* Poll controller (use uint16_t for timer to handle wraparound) */
		uint16_t t0 = TIMER_VALUE(2);
		ControllerState pad;
		pollController(0, &pad);
		uint16_t t1 = TIMER_VALUE(2);
		statPadTime = (uint16_t)(t1 - t0);  /* uint16_t subtraction handles wrap */

		/* Get movement input */
		int16_t moveX = 0;
		int16_t moveZ = 0;

		/* Analog stick input
		 * Left/Right on stick = turn (moveX)
		 * Up/Down on stick = forward/backward (moveZ) */
		if (pad.isAnalog) {
			int stickX = (int)pad.leftX - 0x80;
			int stickY = (int)pad.leftY - 0x80;

			/* X axis: turn left/right */
			if (stickX > ANALOG_DEADZONE) moveX = 1;       /* Turn right */
			else if (stickX < -ANALOG_DEADZONE) moveX = -1; /* Turn left */

			/* Y axis: forward/backward (stick up = forward = positive) */
			if (stickY < -ANALOG_DEADZONE) moveZ = 1;       /* Up on stick = forward */
			else if (stickY > ANALOG_DEADZONE) moveZ = -1;  /* Down on stick = backward */
		}

		/* D-pad input (overrides analog if pressed) */
		if (pad.buttons & PAD_LEFT)  moveX = -1;
		if (pad.buttons & PAD_RIGHT) moveX = 1;
		if (pad.buttons & PAD_UP)    moveZ = 1;   /* Up = forward */
		if (pad.buttons & PAD_DOWN)  moveZ = -1;  /* Down = backward */

		/* X button triggers sound effect and flash */
		if ((pad.buttons & PAD_X) && !(prevButtons & PAD_X)) {
			if (spuSoundAddr != 0) {
				playSample(0, spuSoundAddr, SFX_SAMPLE_RATE, SFX_VOLUME);
			}
			bgFlash = 255;
		}
		prevButtons = pad.buttons;

		/* Fade flash */
		if (bgFlash > 0) {
			bgFlash -= BG_FLASH_FADE_SPEED;
			if (bgFlash < 0) bgFlash = 0;
		}

		/* Store old position for collision response */
		int32_t oldX = player.x;
		int32_t oldZ = player.z;

		/* Update character animation and movement */
		updateCharacter(&player, moveX, moveZ);

		/* Check collision with houses and handle wall sliding */
		int32_t newWorldX = player.x >> 12;  /* Convert to world units */
		int32_t newWorldZ = player.z >> 12;
		int32_t oldWorldX = oldX >> 12;
		int32_t oldWorldZ = oldZ >> 12;

		if (checkAllHouseCollisions(newWorldX, newWorldZ, PLAYER_COLLISION_RADIUS,
		                            houses, NUM_HOUSES)) {
			/* Collision detected - try sliding along walls */
			/* Try moving only in X (keep old Z) */
			bool canMoveX = !checkAllHouseCollisions(newWorldX, oldWorldZ,
			                                          PLAYER_COLLISION_RADIUS,
			                                          houses, NUM_HOUSES);
			/* Try moving only in Z (keep old X) */
			bool canMoveZ = !checkAllHouseCollisions(oldWorldX, newWorldZ,
			                                          PLAYER_COLLISION_RADIUS,
			                                          houses, NUM_HOUSES);

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

		/* Update CD-DA looping */
		// updateCDDA();

		/* Manual camera rotation with L1/R1 bumpers */
		if (pad.buttons & PAD_L1) {
			orbitAngle -= 40;  /* Rotate left */
		}
		if (pad.buttons & PAD_R1) {
			orbitAngle += 40;  /* Rotate right */
		}

		/* Camera follow logic: when player is moving, camera rotates to follow
		 * Moving forward: camera behind player (facing + 2048)
		 * Moving backward: camera in front of player (facing + 0) */
		if (player.isWalking) {
			int16_t targetOrbit = player.facing + (moveZ > 0 ? 2048 : 0);

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

		/* Draw order: floor first (background), then all models together
		 * This helps the ordering table sort models correctly against each other */

		/* 1. Draw floor tiles (background layer) */
		uint16_t t2 = TIMER_VALUE(2);
		drawFloor(chain, &cam);
		uint16_t t3 = TIMER_VALUE(2);
		statFloorTime = (uint16_t)(t3 - t2);

		/* 2. Draw all models (they sort among themselves via ordering table) */
		drawCharacter(chain, &player, &cam);
		for (int i = 0; i < NUM_HOUSES; i++) {
			drawHouse(chain, &houses[i], &cam);
		}
		uint16_t t4 = TIMER_VALUE(2);
		statCharTime = (uint16_t)(t4 - t3);

		/* Calculate gradient colors */
		int topR = BG_TOP_R + ((BG_FLASH_TOP_R - BG_TOP_R) * bgFlash) / 255;
		int topG = BG_TOP_G + ((BG_FLASH_TOP_G - BG_TOP_G) * bgFlash) / 255;
		int topB = BG_TOP_B + ((BG_FLASH_TOP_B - BG_TOP_B) * bgFlash) / 255;
		int botR = BG_BOT_R + ((BG_FLASH_BOT_R - BG_BOT_R) * bgFlash) / 255;
		int botG = BG_BOT_G + ((BG_FLASH_BOT_G - BG_BOT_G) * bgFlash) / 255;
		int botB = BG_BOT_B + ((BG_FLASH_BOT_B - BG_BOT_B) * bgFlash) / 255;

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

		/* Convert times to percentages of frame budget (use long to avoid overflow) */
		int padPct = ((long)statPadTime * 100) / 70416;
		int floorPct = ((long)statFloorTime * 100) / 70416;
		int charPct = ((long)statCharTime * 100) / 70416;

		/* Display performance stats */
		char debugText[96];
		sprintf(debugText, "FPS:%2d Tri:%3d [%s]%2d%%\nPad:%2d%% Floor:%2d%% Char:%2d%%",
		        fps, statTriangles, cpuBar, cpuPercent,
		        padPct, floorPct, charPct);
		printString(chain, &font, 8, 8, debugText);

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
