/*
 * Rendering Implementation - 3D drawing functions
 */

#include "rendering.h"
#include "game_config.h"
#include "transform.h"
#include "trig.h"
#include "ps1/gpucmd.h"
#include "ps1/gte.h"

/* Performance stats */
int statTriangles = 0;
int statTiles = 0;

/* GTE uses 20.12 fixed-point format */
#define ONE (1 << 12)

/* Grass texture info (set by main, used by drawFloor) */
static TextureInfo grassTex;

/* Grass texture dimensions (32x32 16bpp) */
#define GRASS_TEX_WIDTH   32
#define GRASS_TEX_HEIGHT  32

/* Simple hash-based 2D noise for tile variation (returns 0-255) */
static int tileNoise(int x, int z)
{
	/* Hash function - mix coordinates to get pseudo-random value */
	int n = x * 374761393 + z * 668265263;
	n = (n ^ (n >> 13)) * 1274126177;
	return (n ^ (n >> 16)) & 255;
}

/* Apply distance fog to a color component
 * distance: view-space Z distance
 * color: original color component (0-255)
 * fogColor: fog color component (0-255)
 * Returns: fogged color component (0-255) */
static inline uint8_t applyFog(int32_t distance, uint8_t color, uint8_t fogColor)
{
	if (distance <= FOG_NEAR_DISTANCE) return color;
	if (distance >= FOG_FAR_DISTANCE) return fogColor;

	/* Calculate fog factor (0-256, where 256 = fully fogged) */
	int32_t fogRange = FOG_FAR_DISTANCE - FOG_NEAR_DISTANCE;
	int32_t fogDist = distance - FOG_NEAR_DISTANCE;
	int32_t fogFactor = (fogDist * 256) / fogRange;

	/* Interpolate between original color and fog color */
	return (uint8_t)(((256 - fogFactor) * color + fogFactor * fogColor) >> 8);
}

/* Apply fog to RGB color */
void applyFogRGB(int32_t distance, uint8_t *r, uint8_t *g, uint8_t *b)
{
	*r = applyFog(distance, *r, FOG_COLOR_R);
	*g = applyFog(distance, *g, FOG_COLOR_G);
	*b = applyFog(distance, *b, FOG_COLOR_B);
}

/* Initialize grass texture */
void renderingSetGrassTexture(const TextureInfo *tex)
{
	grassTex = *tex;
}

/* Draw floor with textured grass tiles and flat street tiles */
void drawFloor(DMAChain *chain, const Camera *cam)
{
	/* Set up identity rotation matrix (we rotate manually) */
	gte_setRotationMatrix(
		ONE, 0, 0,
		0, ONE, 0,
		0, 0, ONE
	);

	/* Set translation for GTE (zero - we handle translation manually) */
	gte_setControlReg(GTE_TRX, 0);
	gte_setControlReg(GTE_TRY, 0);
	gte_setControlReg(GTE_TRZ, 0);

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

			/* Convert tile to map pixel coordinates for street lookup */
			int32_t tileCenterX = tileX * FLOOR_TILE_SIZE + FLOOR_TILE_SIZE / 2;
			int32_t tileCenterZ = tileZ * FLOOR_TILE_SIZE + FLOOR_TILE_SIZE / 2;
			int mapPixelX = (tileCenterX + MAP_WORLD_SIZE / 2) / MAP_SCALE;
			int mapPixelZ = (MAP_WORLD_SIZE / 2 - tileCenterZ) / MAP_SCALE;

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

			/* Frustum culling in view space */
			if (vz0 < 10 && vz1 < 10 && vz2 < 10 && vz3 < 10) continue;
			if (vx0*2 < -vz0*3 && vx1*2 < -vz1*3 && vx2*2 < -vz2*3 && vx3*2 < -vz3*3) continue;
			if (vx0*2 > vz0*3 && vx1*2 > vz1*3 && vx2*2 > vz2*3 && vx3*2 > vz3*3) continue;

			/* Transform 4 corners of tile with rotated coordinates */
			GTEVector16 v0 = {vx0, vy0, vz0, 0};
			GTEVector16 v1 = {vx1, vy1, vz1, 0};
			GTEVector16 v2 = {vx2, vy2, vz2, 0};
			GTEVector16 v3 = {vx3, vy3, vz3, 0};

			/* Check if this tile is a street */
			int isStreet = IS_STREET_PIXEL(mapPixelX, mapPixelZ);

			/* Count this tile */
			statTiles++;

			/* Calculate average distance for fog (use center of tile) */
			int32_t avgZ = (vz0 + vz1 + vz2 + vz3) / 4;

			if (isStreet) {
				/* Street tile - flat shaded grey */
				uint8_t r, g, b;
				int noise = tileNoise(tileX, tileZ);
				if (noise < 128) {
					r = STREET_COLOR_1_R; g = STREET_COLOR_1_G; b = STREET_COLOR_1_B;
				} else {
					r = STREET_COLOR_2_R; g = STREET_COLOR_2_G; b = STREET_COLOR_2_B;
				}

				/* Apply distance fog */
				applyFogRGB(avgZ, &r, &g, &b);

				/* Triangle 1: v0, v1, v2 */
				gte_loadV0(&v0);
				gte_loadV1(&v1);
				gte_loadV2(&v2);
				gte_command(GTE_CMD_RTPT | GTE_SF);

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
				gte_storeDataReg(GTE_SXY2, 3 * 4, ptr);
				statTriangles++;
			} else {
				/* Grass tile - Gouraud-shaded textured with per-vertex fog */
				/* Calculate per-vertex fog colors (blend from 128 neutral to fog color) */
				uint8_t r0f = 128, g0f = 128, b0f = 128;
				uint8_t r1f = 128, g1f = 128, b1f = 128;
				uint8_t r2f = 128, g2f = 128, b2f = 128;
				uint8_t r3f = 128, g3f = 128, b3f = 128;

				applyFogRGB(vz0, &r0f, &g0f, &b0f);
				applyFogRGB(vz1, &r1f, &g1f, &b1f);
				applyFogRGB(vz2, &r2f, &g2f, &b2f);
				applyFogRGB(vz3, &r3f, &g3f, &b3f);

				/* UV coordinates for grass texture */
				uint8_t u0 = grassTex.u;
				uint8_t v0t = grassTex.v;
				uint8_t u1 = grassTex.u + GRASS_TEX_WIDTH - 1;
				uint8_t v1t = grassTex.v + GRASS_TEX_HEIGHT - 1;

				/* Project vertices for triangle 1: v0, v1, v2 */
				gte_loadV0(&v0);
				gte_loadV1(&v1);
				gte_loadV2(&v2);
				gte_command(GTE_CMD_RTPT | GTE_SF);

				/* Gouraud-shaded textured triangle 1 (9 words) */
				uint32_t *ptr = allocatePacket(chain, ORDERING_TABLE_SIZE - 2, 9);
				ptr[0] = gp0_rgb(r0f, g0f, b0f) | gp0_shadedTriangle(true, true, false);
				gte_storeDataReg(GTE_SXY0, 1 * 4, ptr);  /* XY0 */
				ptr[2] = gp0_uv(u0, v0t, grassTex.clut); /* UV0 + CLUT */
				ptr[3] = gp0_rgb(r1f, g1f, b1f);         /* RGB1 */
				gte_storeDataReg(GTE_SXY1, 4 * 4, ptr);  /* XY1 */
				ptr[5] = gp0_uv(u1, v0t, grassTex.page); /* UV1 + texpage */
				ptr[6] = gp0_rgb(r2f, g2f, b2f);         /* RGB2 */
				gte_storeDataReg(GTE_SXY2, 7 * 4, ptr);  /* XY2 */
				ptr[8] = gp0_uv(u1, v1t, 0);             /* UV2 */
				statTriangles++;

				/* Project vertices for triangle 2: v0, v2, v3 */
				gte_loadV0(&v0);
				gte_loadV1(&v2);
				gte_loadV2(&v3);
				gte_command(GTE_CMD_RTPT | GTE_SF);

				/* Gouraud-shaded textured triangle 2 (9 words) */
				ptr = allocatePacket(chain, ORDERING_TABLE_SIZE - 2, 9);
				ptr[0] = gp0_rgb(r0f, g0f, b0f) | gp0_shadedTriangle(true, true, false);
				gte_storeDataReg(GTE_SXY0, 1 * 4, ptr);  /* XY0 */
				ptr[2] = gp0_uv(u0, v0t, grassTex.clut); /* UV0 + CLUT */
				ptr[3] = gp0_rgb(r2f, g2f, b2f);         /* RGB1 (v2) */
				gte_storeDataReg(GTE_SXY1, 4 * 4, ptr);  /* XY1 */
				ptr[5] = gp0_uv(u1, v1t, grassTex.page); /* UV1 + texpage */
				ptr[6] = gp0_rgb(r3f, g3f, b3f);         /* RGB2 (v3) */
				gte_storeDataReg(GTE_SXY2, 7 * 4, ptr);  /* XY2 */
				ptr[8] = gp0_uv(u0, v1t, 0);             /* UV2 */
				statTriangles++;
			}
		}
	}
}

/* Draw a static house model at world position */
void drawHouse(DMAChain *chain, const House *house, const Camera *cam)
{
	/* Calculate house position relative to camera (in world space) */
	int32_t relX = house->x - cam->x;
	int32_t relY = house->y - cam->y;
	int32_t relZ = house->z - cam->z;

	/* Early distance cull - skip houses too far from camera */
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

	/* Build combined rotation: viewRotation * houseYawRotation */
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

		/* Apply distance fog to all vertices */
		applyFogRGB(viewZ, &r0, &g0, &b0);
		applyFogRGB(viewZ, &r1, &g1, &b1);
		applyFogRGB(viewZ, &r2, &g2, &b2);

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

/* Set up GTE for tree batch rendering */
void setupTreeBatch(const Camera *cam)
{
	matrixLoadToGTE(&cam->viewRotation);
}

/* Draw a tree model at world position */
void drawTree(DMAChain *chain, const Tree *tree, const Camera *cam)
{
	/* Calculate tree position relative to camera */
	int32_t relX = tree->x - cam->x;
	int32_t relY = tree->y - cam->y;
	int32_t relZ = tree->z - cam->z;

	/* Early distance cull */
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

	/* Set translation */
	gte_setControlReg(GTE_TRX, viewX);
	gte_setControlReg(GTE_TRY, viewY);
	gte_setControlReg(GTE_TRZ, viewZ);

	/* Draw all faces */
	const Model *model = &tree->model;
	for (int i = 0; i < model->numFaces; i++) {
		const Face *face = &model->faces[i];

		/* Scale vertices by TREE_SCALE */
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

		/* Apply distance fog to vertex colors */
		applyFogRGB(viewZ, &r0, &g0, &b0);
		applyFogRGB(viewZ, &r1, &g1, &b1);
		applyFogRGB(viewZ, &r2, &g2, &b2);

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

/* Set up GTE for fence batch rendering */
void setupFenceBatch(const Camera *cam)
{
	matrixLoadToGTE(&cam->viewRotation);
}

/* Draw a fence post as a tile-aligned vertical quad */
void drawFencePost(DMAChain *chain, const FencePost *post, const Camera *cam)
{
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

	/* Apply distance fog to fence colors */
	uint8_t fenceR = FENCE_COLOR_R;
	uint8_t fenceG = FENCE_COLOR_G;
	uint8_t fenceB = FENCE_COLOR_B;
	applyFogRGB(viewZ, &fenceR, &fenceG, &fenceB);

	/* Darker back face colors with fog */
	uint8_t backR = (FENCE_COLOR_R > 15) ? FENCE_COLOR_R - 15 : 0;
	uint8_t backG = (FENCE_COLOR_G > 10) ? FENCE_COLOR_G - 10 : 0;
	uint8_t backB = (FENCE_COLOR_B > 5) ? FENCE_COLOR_B - 5 : 0;
	applyFogRGB(viewZ, &backR, &backG, &backB);

	/* Set GTE translation */
	gte_setControlReg(GTE_TRX, viewX);
	gte_setControlReg(GTE_TRY, viewY);
	gte_setControlReg(GTE_TRZ, viewZ);

	/* Fence is a flat quad spanning the full map tile */
	int16_t halfWidth = MAP_SCALE / 2;
	int16_t topY = -FENCE_HEIGHT;
	int16_t botY = 0;

	uint32_t *ptr;
	int zIndex;

	/* Define wall vertices based on orientation */
	GTEVector16 v0, v1, v2, v3;

	switch (post->orientation) {
		case 0:  /* N-S wall (|) - spans Z axis */
			v0 = (GTEVector16){0, botY, -halfWidth, 0};
			v1 = (GTEVector16){0, botY, halfWidth, 0};
			v2 = (GTEVector16){0, topY, halfWidth, 0};
			v3 = (GTEVector16){0, topY, -halfWidth, 0};
			break;
		case 1:  /* E-W wall (-) - spans X axis */
			v0 = (GTEVector16){-halfWidth, botY, 0, 0};
			v1 = (GTEVector16){halfWidth, botY, 0, 0};
			v2 = (GTEVector16){halfWidth, topY, 0, 0};
			v3 = (GTEVector16){-halfWidth, topY, 0, 0};
			break;
		case 2:  /* Diagonal NE-SW (\) */
			v0 = (GTEVector16){-halfWidth, botY, -halfWidth, 0};
			v1 = (GTEVector16){halfWidth, botY, halfWidth, 0};
			v2 = (GTEVector16){halfWidth, topY, halfWidth, 0};
			v3 = (GTEVector16){-halfWidth, topY, -halfWidth, 0};
			break;
		case 3:  /* Diagonal NW-SE (/) */
			v0 = (GTEVector16){-halfWidth, botY, halfWidth, 0};
			v1 = (GTEVector16){halfWidth, botY, -halfWidth, 0};
			v2 = (GTEVector16){halfWidth, topY, -halfWidth, 0};
			v3 = (GTEVector16){-halfWidth, topY, halfWidth, 0};
			break;
		default:
			return;
	}

	/* Front face - triangle 1 */
	gte_loadV0(&v0);
	gte_loadV1(&v1);
	gte_loadV2(&v2);
	gte_command(GTE_CMD_RTPT | GTE_SF);
	gte_command(GTE_CMD_AVSZ3 | GTE_SF);
	zIndex = gte_getDataReg(GTE_OTZ);
	if (zIndex >= 0 && zIndex < ORDERING_TABLE_SIZE) {
		ptr = allocatePacket(chain, zIndex, 4);
		ptr[0] = gp0_rgb(fenceR, fenceG, fenceB) | gp0_triangle(false, false);
		gte_storeDataReg(GTE_SXY0, 1 * 4, ptr);
		gte_storeDataReg(GTE_SXY1, 2 * 4, ptr);
		gte_storeDataReg(GTE_SXY2, 3 * 4, ptr);
		statTriangles++;
	}

	/* Front face - triangle 2 */
	gte_loadV0(&v0);
	gte_loadV1(&v2);
	gte_loadV2(&v3);
	gte_command(GTE_CMD_RTPT | GTE_SF);
	gte_command(GTE_CMD_AVSZ3 | GTE_SF);
	zIndex = gte_getDataReg(GTE_OTZ);
	if (zIndex >= 0 && zIndex < ORDERING_TABLE_SIZE) {
		ptr = allocatePacket(chain, zIndex, 4);
		ptr[0] = gp0_rgb(fenceR, fenceG, fenceB) | gp0_triangle(false, false);
		gte_storeDataReg(GTE_SXY0, 1 * 4, ptr);
		gte_storeDataReg(GTE_SXY1, 2 * 4, ptr);
		gte_storeDataReg(GTE_SXY2, 3 * 4, ptr);
		statTriangles++;
	}

	/* Back face (reversed winding) - triangle 1 */
	gte_loadV0(&v1);
	gte_loadV1(&v0);
	gte_loadV2(&v3);
	gte_command(GTE_CMD_RTPT | GTE_SF);
	gte_command(GTE_CMD_AVSZ3 | GTE_SF);
	zIndex = gte_getDataReg(GTE_OTZ);
	if (zIndex >= 0 && zIndex < ORDERING_TABLE_SIZE) {
		ptr = allocatePacket(chain, zIndex, 4);
		ptr[0] = gp0_rgb(backR, backG, backB) | gp0_triangle(false, false);
		gte_storeDataReg(GTE_SXY0, 1 * 4, ptr);
		gte_storeDataReg(GTE_SXY1, 2 * 4, ptr);
		gte_storeDataReg(GTE_SXY2, 3 * 4, ptr);
		statTriangles++;
	}

	/* Back face - triangle 2 */
	gte_loadV0(&v1);
	gte_loadV1(&v3);
	gte_loadV2(&v2);
	gte_command(GTE_CMD_RTPT | GTE_SF);
	gte_command(GTE_CMD_AVSZ3 | GTE_SF);
	zIndex = gte_getDataReg(GTE_OTZ);
	if (zIndex >= 0 && zIndex < ORDERING_TABLE_SIZE) {
		ptr = allocatePacket(chain, zIndex, 4);
		ptr[0] = gp0_rgb(backR, backG, backB) | gp0_triangle(false, false);
		gte_storeDataReg(GTE_SXY0, 1 * 4, ptr);
		gte_storeDataReg(GTE_SXY1, 2 * 4, ptr);
		gte_storeDataReg(GTE_SXY2, 3 * 4, ptr);
		statTriangles++;
	}
}

#if DEBUG_DRAW_COLLISION

/* Screen constants for manual projection */
#define DEBUG_CENTERX 160  /* 320 / 2 */
#define DEBUG_CENTERY 120  /* 240 / 2 */

/* Draw a single 2D line */
static void drawLine2D(DMAChain *chain, int zIndex,
	int16_t x1, int16_t y1, int16_t x2, int16_t y2,
	uint8_t r, uint8_t g, uint8_t b)
{
	if (zIndex < 0 || zIndex >= ORDERING_TABLE_SIZE) return;

	/* Clip to screen bounds */
	if (x1 < -512 || x1 > 512 || y1 < -512 || y1 > 512) return;
	if (x2 < -512 || x2 > 512 || y2 < -512 || y2 > 512) return;

	uint32_t *ptr = allocatePacket(chain, zIndex, 3);
	ptr[0] = gp0_rgb(r, g, b) | gp0_line(false, false);
	ptr[1] = gp0_xy(x1, y1);
	ptr[2] = gp0_xy(x2, y2);
}

/* Draw wireframe collision boxes for a house */
void drawHouseCollisionDebug(DMAChain *chain, const House *house, const Camera *cam)
{
	/* Early distance cull */
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

		/* Box corners in world space */
		int32_t worldY = FLOOR_Y;

		int32_t corners[4][3] = {
			{house->x + scaledMinX, worldY, house->z + scaledMinZ},
			{house->x + scaledMaxX, worldY, house->z + scaledMinZ},
			{house->x + scaledMaxX, worldY, house->z + scaledMaxZ},
			{house->x + scaledMinX, worldY, house->z + scaledMaxZ}
		};

		/* Transform and project corners */
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

		if (!allVisible) continue;

		int zIndex = (avgZ / 4) / (32768 / ORDERING_TABLE_SIZE);
		if (zIndex >= ORDERING_TABLE_SIZE) zIndex = ORDERING_TABLE_SIZE - 1;
		if (zIndex < 0) zIndex = 0;

		/* Draw 4 lines (green) */
		drawLine2D(chain, zIndex, screenX[0], screenY[0], screenX[1], screenY[1], 0, 255, 0);
		drawLine2D(chain, zIndex, screenX[1], screenY[1], screenX[2], screenY[2], 0, 255, 0);
		drawLine2D(chain, zIndex, screenX[2], screenY[2], screenX[3], screenY[3], 0, 255, 0);
		drawLine2D(chain, zIndex, screenX[3], screenY[3], screenX[0], screenY[0], 0, 255, 0);
	}
}

/* Draw collision debug for all houses */
void drawAllCollisionDebug(DMAChain *chain, const House *houses, int numHouses,
	const Camera *cam)
{
	for (int i = 0; i < numHouses; i++) {
		drawHouseCollisionDebug(chain, &houses[i], cam);
	}
}

/* Draw door trigger debug wireframe */
void drawDoorTriggerDebug(DMAChain *chain, const House *house, const Camera *cam)
{
	/* Early distance cull */
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

	/* Rotate door offset based on house rotation */
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

	int32_t doorCenterX = house->x + worldOffsetX;
	int32_t doorCenterZ = house->z + worldOffsetZ;
	int32_t worldY = FLOOR_Y;

	int32_t corners[4][3] = {
		{doorCenterX - scaledSizeX, worldY, doorCenterZ - scaledSizeZ},
		{doorCenterX + scaledSizeX, worldY, doorCenterZ - scaledSizeZ},
		{doorCenterX + scaledSizeX, worldY, doorCenterZ + scaledSizeZ},
		{doorCenterX - scaledSizeX, worldY, doorCenterZ + scaledSizeZ}
	};

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

	/* Draw 4 lines (red) */
	drawLine2D(chain, zIndex, screenX[0], screenY[0], screenX[1], screenY[1], 255, 0, 0);
	drawLine2D(chain, zIndex, screenX[1], screenY[1], screenX[2], screenY[2], 255, 0, 0);
	drawLine2D(chain, zIndex, screenX[2], screenY[2], screenX[3], screenY[3], 255, 0, 0);
	drawLine2D(chain, zIndex, screenX[3], screenY[3], screenX[0], screenY[0], 255, 0, 0);
}

/* Draw door trigger debug for all houses */
void drawAllDoorTriggersDebug(DMAChain *chain, const House *houses, int numHouses,
	const Camera *cam)
{
	for (int i = 0; i < numHouses; i++) {
		drawDoorTriggerDebug(chain, &houses[i], cam);
	}
}

/* Draw a rectangular outline in world space */
static void drawRectDebug(DMAChain *chain, const Camera *cam,
	int32_t centerX, int32_t centerZ, int32_t halfX, int32_t halfZ,
	uint8_t r, uint8_t g, uint8_t b)
{
	int32_t worldY = FLOOR_Y;

	int32_t corners[4][3] = {
		{centerX - halfX, worldY, centerZ - halfZ},
		{centerX + halfX, worldY, centerZ - halfZ},
		{centerX + halfX, worldY, centerZ + halfZ},
		{centerX - halfX, worldY, centerZ + halfZ}
	};

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

	drawLine2D(chain, zIndex, screenX[0], screenY[0], screenX[1], screenY[1], r, g, b);
	drawLine2D(chain, zIndex, screenX[1], screenY[1], screenX[2], screenY[2], r, g, b);
	drawLine2D(chain, zIndex, screenX[2], screenY[2], screenX[3], screenY[3], r, g, b);
	drawLine2D(chain, zIndex, screenX[3], screenY[3], screenX[0], screenY[0], r, g, b);
}

/* Draw interior debug */
void drawInteriorDebug(DMAChain *chain, const Camera *cam,
	int32_t doorOffsetX, int32_t doorOffsetZ,
	int32_t doorSizeX, int32_t doorSizeZ,
	int32_t floorHalfX, int32_t floorHalfZ)
{
	/* Draw floor bounds (cyan) */
	drawRectDebug(chain, cam, 0, 0, floorHalfX, floorHalfZ, 0, 255, 255);

	/* Draw door trigger (red) */
	drawRectDebug(chain, cam, doorOffsetX, doorOffsetZ, doorSizeX, doorSizeZ, 255, 0, 0);
}

#endif /* DEBUG_DRAW_COLLISION */
