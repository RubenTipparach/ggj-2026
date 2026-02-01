/*
 * Rendering - 3D drawing functions for game objects
 */

#pragma once

#include <stdint.h>
#include "gpu.h"
#include "camera.h"
#include "game_types.h"
#include "world_data.h"

/* Performance stats (updated by rendering functions) */
extern int statTriangles;  /* Triangles rendered this frame */
extern int statTiles;      /* Floor tiles rendered this frame */

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize grass texture (call after uploading texture to VRAM) */
void renderingSetGrassTexture(const TextureInfo *tex);

/* Apply distance fog to RGB color */
void applyFogRGB(int32_t distance, uint8_t *r, uint8_t *g, uint8_t *b);

/* Draw the floor tiles (grass and street) */
void drawFloor(DMAChain *chain, const Camera *cam);

/* Draw a static house model at world position */
void drawHouse(DMAChain *chain, const House *house, const Camera *cam);

/* Set up GTE for tree batch rendering (call once before drawing all trees) */
void setupTreeBatch(const Camera *cam);

/* Draw a tree model at world position (no rotation) - call setupTreeBatch first */
void drawTree(DMAChain *chain, const Tree *tree, const Camera *cam);

/* Set up GTE for fence batch rendering (call once before drawing all fences) */
void setupFenceBatch(const Camera *cam);

/* Draw a fence post as a tile-aligned vertical quad */
void drawFencePost(DMAChain *chain, const FencePost *post, const Camera *cam);

#if DEBUG_DRAW_COLLISION
/* Draw wireframe collision boxes for a house */
void drawHouseCollisionDebug(DMAChain *chain, const House *house, const Camera *cam);

/* Draw collision debug for all houses */
void drawAllCollisionDebug(DMAChain *chain, const House *houses, int numHouses,
	const Camera *cam);

/* Draw door trigger debug wireframe (red) */
void drawDoorTriggerDebug(DMAChain *chain, const House *house, const Camera *cam);

/* Draw door trigger debug for all houses */
void drawAllDoorTriggersDebug(DMAChain *chain, const House *houses, int numHouses,
	const Camera *cam);

/* Draw interior debug (floor bounds and door trigger) */
void drawInteriorDebug(DMAChain *chain, const Camera *cam,
	int32_t doorOffsetX, int32_t doorOffsetZ,
	int32_t doorSizeX, int32_t doorSizeZ,
	int32_t floorHalfX, int32_t floorHalfZ);
#endif /* DEBUG_DRAW_COLLISION */

#ifdef __cplusplus
}
#endif
