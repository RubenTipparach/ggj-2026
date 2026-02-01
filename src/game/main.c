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
#include "dialog.h"
#include "world_data.h"
#include "adult_female_L_offsets.h"

/* Simple hash-based 2D noise for tile variation (returns 0-255) */
static int tileNoise(int x, int z) {
	/* Hash function - mix coordinates to get pseudo-random value */
	int n = x * 374761393 + z * 668265263;
	n = (n ^ (n >> 13)) * 1274126177;
	return (n ^ (n >> 16)) & 255;
}

/* Apply distance fog to a color component
* distance: view-space Z distance
* color: original color component (0-255)
* fogColor: fog color component (0-255)
* Returns: fogged color component (0-255)
*/
static inline uint8_t applyFog(int32_t distance, uint8_t color, uint8_t fogColor) {
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
static inline void applyFogRGB(int32_t distance, uint8_t *r, uint8_t *g, uint8_t *b) {
	*r = applyFog(distance, *r, FOG_COLOR_R);
	*g = applyFog(distance, *g, FOG_COLOR_G);
	*b = applyFog(distance, *b, FOG_COLOR_B);
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

/* Restaurant model data embedded by CMake */
extern const uint8_t restaurantData[];
extern const uint32_t restaurantData_size;
extern const uint8_t restaurantIntData[];
extern const uint32_t restaurantIntData_size;

/* Mom character data embedded by CMake */
extern const uint8_t momBodyData[];
extern const uint32_t momBodyData_size;
extern const uint8_t momHeadData[];
extern const uint32_t momHeadData_size;
extern const uint8_t momArmLeftData[];
extern const uint32_t momArmLeftData_size;
extern const uint8_t momArmRightData[];
extern const uint32_t momArmRightData_size;
extern const uint8_t momLegLeftData[];
extern const uint32_t momLegLeftData_size;
extern const uint8_t momLegRightData[];
extern const uint32_t momLegRightData_size;

/* Food box model data embedded by CMake */
extern const uint8_t foodBoxData[];
extern const uint32_t foodBoxData_size;

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

/* Day intro display duration (frames at 60fps = 5 seconds) */
#define DAY_INTRO_DURATION (60 * 5)

/* Typewriter effect speed (characters per frame) */
#define INTRO_TEXT_SPEED 1

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
						
						/* Check if player is at a single building's door (for restaurant) */
						static bool isAtBuildingDoor(int32_t playerX, int32_t playerZ, const House *building) {
							const DoorTrigger *door = &building->door;
							
							/* Scale door offset and size by RESTAURANT_SCALE */
							int32_t scaledOffsetX = (door->offsetX * RESTAURANT_SCALE) >> 12;
							int32_t scaledOffsetZ = (door->offsetZ * RESTAURANT_SCALE) >> 12;
							int32_t scaledSizeX = (door->sizeX * RESTAURANT_SCALE) >> 12;
							int32_t scaledSizeZ = (door->sizeZ * RESTAURANT_SCALE) >> 12;
							
							/* Rotate door offset based on building rotation */
							int16_t rot = building->rotation;
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
							
							int32_t doorCenterX = building->x + worldOffsetX;
							int32_t doorCenterZ = building->z + worldOffsetZ;
							
							return (playerX >= doorCenterX - scaledSizeX &&
								playerX <= doorCenterX + scaledSizeX &&
								playerZ >= doorCenterZ - scaledSizeZ &&
								playerZ <= doorCenterZ + scaledSizeZ);
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
														int32_t doorSizeX, int32_t doorSizeZ,
														int32_t floorHalfX, int32_t floorHalfZ) {
															/* Draw floor bounds (cyan) */
															drawRectDebug(chain, cam, 0, 0,
																floorHalfX, floorHalfZ,
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
																
																/* Grass texture data embedded by CMake (32x32 16bpp) */
																extern const uint8_t grassTexture[];
																
																/* Music data embedded by CMake (SPU-ADPCM format) */
																extern const uint8_t musicData[];
																extern const uint32_t musicData_size;
																
																/* Font dimensions */
																#define FONT_WIDTH        96
																#define FONT_HEIGHT       56
																#define FONT_COLOR_DEPTH  GP0_COLOR_4BPP
																
																/* Grass texture dimensions (32x32 16bpp) */
																#define GRASS_TEX_WIDTH   32
																#define GRASS_TEX_HEIGHT  32
																
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
																
																/* Grass texture info (uploaded in main, used by drawFloor) */
																static TextureInfo grassTex;
																
																/* Draw floor with textured grass tiles and flat street tiles */
																static void drawFloor(DMAChain *chain, const Camera *cam) {
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
																				
																				/* Fence is a flat quad spanning the full map tile (MAP_SCALE x FENCE_HEIGHT) */
																				int16_t halfWidth = MAP_SCALE / 2;
																				/* Y=0 is at floor level, negative Y is above the floor */
																				int16_t topY = -FENCE_HEIGHT;
																				int16_t botY = 0;
																				
																				uint32_t *ptr;
																				int zIndex;
																				
																				/* Define wall vertices based on orientation:
																				* 0 = N-S wall (|) - spans Z axis, blocks E-W movement
																				* 1 = E-W wall (-) - spans X axis, blocks N-S movement
																				* 2 = diagonal NE-SW (\)
																				* 3 = diagonal NW-SE (/)
																				*/
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
																						
																						/* Draw the pause screen map view */
																						static void drawPauseMap(DMAChain *chain, int32_t playerWorldX, int32_t playerWorldZ, int16_t playerFacing, int frameCounter) {
																							/* Map display area - centered on screen (320x240), smaller size */
																							const int mapScreenSize = 128;  /* Square map area */
																							const int mapScreenX = (SCREEN_WIDTH - mapScreenSize) / 2;   /* Center horizontally */
																							const int mapScreenY = (SCREEN_HEIGHT - mapScreenSize) / 2;  /* Center vertically */
																							
																							/* Scale: map pixels to screen pixels (8.8 fixed point) */
																							int scale = (mapScreenSize << 8) / MAP_PIXELS;
																							
																							/* Draw map background at OT index 2 (behind everything) */
																							uint32_t *ptr = allocatePacket(chain, 2, 3);
																							ptr[0] = gp0_rgb(25, 40, 60) | gp0_rectangle(false, false, false);
																							ptr[1] = gp0_xy(mapScreenX - 4, mapScreenY - 4);
																							ptr[2] = gp0_xy(mapScreenSize + 8, mapScreenSize + 8);
																							
																							/* Draw border frame */
																							ptr = allocatePacket(chain, 0, 3);
																							ptr[0] = gp0_rgb(80, 100, 120) | gp0_rectangle(false, false, false);
																							ptr[1] = gp0_xy(mapScreenX - 5, mapScreenY - 5);
																							ptr[2] = gp0_xy(mapScreenSize + 10, 2);  /* Top border */
																							
																							ptr = allocatePacket(chain, 0, 3);
																							ptr[0] = gp0_rgb(80, 100, 120) | gp0_rectangle(false, false, false);
																							ptr[1] = gp0_xy(mapScreenX - 5, mapScreenY + mapScreenSize + 3);
																							ptr[2] = gp0_xy(mapScreenSize + 10, 2);  /* Bottom border */
																							
																							ptr = allocatePacket(chain, 0, 3);
																							ptr[0] = gp0_rgb(80, 100, 120) | gp0_rectangle(false, false, false);
																							ptr[1] = gp0_xy(mapScreenX - 5, mapScreenY - 5);
																							ptr[2] = gp0_xy(2, mapScreenSize + 10);  /* Left border */
																							
																							ptr = allocatePacket(chain, 0, 3);
																							ptr[0] = gp0_rgb(80, 100, 120) | gp0_rectangle(false, false, false);
																							ptr[1] = gp0_xy(mapScreenX + mapScreenSize + 3, mapScreenY - 5);
																							ptr[2] = gp0_xy(2, mapScreenSize + 10);  /* Right border */
																							
																							/* Draw street tiles at OT index 1 (behind player arrow) */
																							int tileSize = (scale >> 8) + 1;
																							for (int py = 0; py < MAP_PIXELS; py++) {
																								for (int px = 0; px < MAP_PIXELS; px++) {
																									if (IS_STREET_PIXEL(px, py)) {
																										int screenX = mapScreenX + ((px * scale) >> 8);
																										int screenY = mapScreenY + ((py * scale) >> 8);
																										
																										ptr = allocatePacket(chain, 1, 3);
																										ptr[0] = gp0_rgb(100, 100, 110) | gp0_rectangle(false, false, false);
																										ptr[1] = gp0_xy(screenX, screenY);
																										ptr[2] = gp0_xy(tileSize, tileSize);
																									}
																								}
																							}
																							
																							/* Draw fence posts as brown dots at OT index 1 */
																							for (int i = 0; i < NUM_FENCE_POSTS; i++) {
																								/* Convert world pos to map pixel (negate Z to match world coords) */
																								int px = (mapFencePosts[i].x + MAP_WORLD_SIZE / 2) / MAP_SCALE;
																								int pz = (MAP_WORLD_SIZE / 2 - mapFencePosts[i].z) / MAP_SCALE;
																								
																								int screenX = mapScreenX + ((px * scale) >> 8);
																								int screenY = mapScreenY + ((pz * scale) >> 8);
																								
																								ptr = allocatePacket(chain, 1, 3);
																								ptr[0] = gp0_rgb(140, 100, 60) | gp0_rectangle(false, false, false);
																								ptr[1] = gp0_xy(screenX, screenY);
																								ptr[2] = gp0_xy(2, 2);
																							}
																							
																							/* Draw houses as red squares at OT index 1 */
																							for (int i = 0; i < NUM_MAP_HOUSES; i++) {
																								int px = (mapHouses[i].x + MAP_WORLD_SIZE / 2) / MAP_SCALE;
																								int pz = (MAP_WORLD_SIZE / 2 - mapHouses[i].z) / MAP_SCALE;
																								
																								int screenX = mapScreenX + ((px * scale) >> 8) - 2;
																								int screenY = mapScreenY + ((pz * scale) >> 8) - 2;
																								
																								ptr = allocatePacket(chain, 1, 3);
																								ptr[0] = gp0_rgb(200, 80, 100) | gp0_rectangle(false, false, false);
																								ptr[1] = gp0_xy(screenX, screenY);
																								ptr[2] = gp0_xy(5, 5);
																							}
																							
																							/* Draw trees as green dots at OT index 1 */
																							for (int i = 0; i < NUM_MAP_TREES; i++) {
																								int px = (mapTrees[i].x + MAP_WORLD_SIZE / 2) / MAP_SCALE;
																								int pz = (MAP_WORLD_SIZE / 2 - mapTrees[i].z) / MAP_SCALE;
																								
																								int screenX = mapScreenX + ((px * scale) >> 8);
																								int screenY = mapScreenY + ((pz * scale) >> 8);
																								
																								ptr = allocatePacket(chain, 1, 3);
																								ptr[0] = gp0_rgb(50, 150, 80) | gp0_rectangle(false, false, false);
																								ptr[1] = gp0_xy(screenX, screenY);
																								ptr[2] = gp0_xy(3, 3);
																							}
																							
																							/* Draw player position as blinking yellow arrow showing direction at OT index 0 (on top)
																							* Game facing: 0=North(-Z), 1024=East(+X), 2048=South(+Z), 3072=West(-X)
																							* Screen coords: X right, Y down
																							* Map Z is negated to match world coordinate flip */
																							if ((frameCounter / 15) % 2 == 0) {  /* Blink every 15 frames */
																								int playerPx = (playerWorldX + MAP_WORLD_SIZE / 2) / MAP_SCALE;
																								int playerPz = (MAP_WORLD_SIZE / 2 - playerWorldZ) / MAP_SCALE;
																								
																								int centerX = mapScreenX + ((playerPx * scale) >> 8);
																								int centerY = mapScreenY + ((playerPz * scale) >> 8);
																								
																								/* Direction vector: facing 0 = up (-Y), 1024 = right (+X), 2048 = down (+Y)
																								* Negate dirY because map Z is flipped relative to world Z
																								* isin/icos return 12-bit fixed point (4096 = 1.0) */
																								int dirX = isin(playerFacing);
																								int dirY = -icos(playerFacing);
																								
																								/* Arrow size in pixels */
																								const int arrowFront = 5;  /* Distance from center to tip */
																								const int arrowBack = 3;   /* Distance from center to back */
																								const int arrowSide = 3;   /* Half-width at back */
																								
																								/* Calculate arrow vertices */
																								int tipX = centerX + ((dirX * arrowFront) >> 12);
																								int tipY = centerY + ((dirY * arrowFront) >> 12);
																								
																								int backX = centerX - ((dirX * arrowBack) >> 12);
																								int backY = centerY - ((dirY * arrowBack) >> 12);
																								
																								/* Perpendicular direction: 90 degrees CW = (dirY, -dirX) */
																								int leftX = backX - ((dirY * arrowSide) >> 12);
																								int leftY = backY + ((dirX * arrowSide) >> 12);
																								
																								int rightX = backX + ((dirY * arrowSide) >> 12);
																								int rightY = backY - ((dirX * arrowSide) >> 12);
																								
																								/* Draw arrow as bright yellow triangle */
																								ptr = allocatePacket(chain, 0, 4);
																								ptr[0] = gp0_rgb(255, 255, 0) | gp0_triangle(false, false);
																								ptr[1] = gp0_xy(tipX, tipY);
																								ptr[2] = gp0_xy(leftX, leftY);
																								ptr[3] = gp0_xy(rightX, rightY);
																							}
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
																							
																							/* Upload grass texture to VRAM (32x32 16bpp) */
																							uploadTexture(
																								&grassTex,
																								grassTexture,
																								SCREEN_WIDTH * 2,          /* Image X = 640 */
																								320,                       /* Image Y = 320 (below font) */
																								GRASS_TEX_WIDTH,
																								GRASS_TEX_HEIGHT
																							);
																							puts("Grass texture uploaded to VRAM");
																							
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

																								/* Initialize mom character (NPC in restaurant) */
																								Character mom;
																								if (!initCharacter(&mom,
																									momBodyData, momBodyData_size,
																									momHeadData, momHeadData_size,
																									momArmLeftData, momArmLeftData_size,
																									momArmRightData, momArmRightData_size,
																									momLegLeftData, momLegLeftData_size,
																									momLegRightData, momLegRightData_size))
																								{
																									puts("Failed to initialize mom character!");
																									return 1;
																								}
																								/* Set mom's offsets from adult_female_L header */
																								mom.partOffsetX[PART_BODY] = ADULT_FEMALE_L_BODY_OFFSET_X;
																								mom.partOffsetY[PART_BODY] = ADULT_FEMALE_L_BODY_OFFSET_Y;
																								mom.partOffsetZ[PART_BODY] = ADULT_FEMALE_L_BODY_OFFSET_Z;
																								mom.partOffsetX[PART_HEAD] = ADULT_FEMALE_L_HEAD_OFFSET_X;
																								mom.partOffsetY[PART_HEAD] = ADULT_FEMALE_L_HEAD_OFFSET_Y;
																								mom.partOffsetZ[PART_HEAD] = ADULT_FEMALE_L_HEAD_OFFSET_Z;
																								mom.partOffsetX[PART_ARM_LEFT] = ADULT_FEMALE_L_ARM_LEFT_OFFSET_X;
																								mom.partOffsetY[PART_ARM_LEFT] = ADULT_FEMALE_L_ARM_LEFT_OFFSET_Y;
																								mom.partOffsetZ[PART_ARM_LEFT] = ADULT_FEMALE_L_ARM_LEFT_OFFSET_Z;
																								mom.partOffsetX[PART_ARM_RIGHT] = ADULT_FEMALE_L_ARM_RIGHT_OFFSET_X;
																								mom.partOffsetY[PART_ARM_RIGHT] = ADULT_FEMALE_L_ARM_RIGHT_OFFSET_Y;
																								mom.partOffsetZ[PART_ARM_RIGHT] = ADULT_FEMALE_L_ARM_RIGHT_OFFSET_Z;
																								mom.partOffsetX[PART_LEG_LEFT] = ADULT_FEMALE_L_LEG_LEFT_OFFSET_X;
																								mom.partOffsetY[PART_LEG_LEFT] = ADULT_FEMALE_L_LEG_LEFT_OFFSET_Y;
																								mom.partOffsetZ[PART_LEG_LEFT] = ADULT_FEMALE_L_LEG_LEFT_OFFSET_Z;
																								mom.partOffsetX[PART_LEG_RIGHT] = ADULT_FEMALE_L_LEG_RIGHT_OFFSET_X;
																								mom.partOffsetY[PART_LEG_RIGHT] = ADULT_FEMALE_L_LEG_RIGHT_OFFSET_Y;
																								mom.partOffsetZ[PART_LEG_RIGHT] = ADULT_FEMALE_L_LEG_RIGHT_OFFSET_Z;
																								/* Position mom in the restaurant interior */
																								mom.x = MOM_POS_X << 12;
																								mom.y = 0;
																								mom.z = MOM_POS_Z << 12;
																								mom.facing = 0;
																								puts("Mom character initialized!");

																								/* Load food box model */
																								Model foodBoxModel;
																								if (!loadCharacterModel(&foodBoxModel, foodBoxData, foodBoxData_size)) {
																									puts("Failed to load food box model!");
																									return 1;
																								}
																								puts("Food box model loaded!");

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
																										
																										/* Load restaurant models */
																										Model restaurantModel;
																										if (!loadCharacterModel(&restaurantModel, restaurantData, restaurantData_size)) {
																											puts("Failed to load restaurant model!");
																											return 1;
																										}
																										Model restaurantInterior;
																										if (!loadCharacterModel(&restaurantInterior, restaurantIntData, restaurantIntData_size)) {
																											puts("Failed to load restaurant interior!");
																											return 1;
																										}
																										printf("Restaurant loaded: %d exterior, %d interior faces\n",
																											restaurantModel.numFaces, restaurantInterior.numFaces);
																											
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
																											
																											/* Initialize restaurant at player spawn location */
																											House restaurant;
																											restaurant.model = restaurantModel;
																											restaurant.x = PLAYER_SPAWN_X;
																											restaurant.y = FLOOR_Y;
																											restaurant.z = PLAYER_SPAWN_Z;
																											restaurant.rotation = RESTAURANT_ROTATION;
																											restaurant.numCollisionBoxes = 1;
																											restaurant.collisionBoxes[0].minX = -RESTAURANT_COLLISION_SIZE_X;
																											restaurant.collisionBoxes[0].minZ = -RESTAURANT_COLLISION_SIZE_Z;
																											restaurant.collisionBoxes[0].maxX = RESTAURANT_COLLISION_SIZE_X;
																											restaurant.collisionBoxes[0].maxZ = RESTAURANT_COLLISION_SIZE_Z;
																											restaurant.door.offsetX = RESTAURANT_DOOR_OFFSET_X;
																											restaurant.door.offsetZ = RESTAURANT_DOOR_OFFSET_Z;
																											restaurant.door.sizeX = RESTAURANT_DOOR_SIZE_X;
																											restaurant.door.sizeZ = RESTAURANT_DOOR_SIZE_Z;
																											printf("Restaurant initialized at (%d, %d)\n", restaurant.x, restaurant.z);
																											
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
																													trees[i].y = FLOOR_Y + TREE_Y_OFFSET;
																													trees[i].z = spawn->z;
																												}
																												printf("Initialized %d trees from map data\n", NUM_MAP_TREES);
																												
																												/* Set player spawn position from map data with offset (convert to 20.12 fixed point) */
																												player.x = (PLAYER_SPAWN_X + PLAYER_SPAWN_OFFSET_X) << 12;
																												player.z = (PLAYER_SPAWN_Z + PLAYER_SPAWN_OFFSET_Z) << 12;
																												player.facing = PLAYER_START_ROTATION;
																												player.targetFacing = PLAYER_START_ROTATION;
																												printf("Player spawn: %d, %d (with offset)\n", PLAYER_SPAWN_X + PLAYER_SPAWN_OFFSET_X, PLAYER_SPAWN_Z + PLAYER_SPAWN_OFFSET_Z);
																												
																												/* Double buffering */
																												DMAChain dmaChains[2];
																												bool     usingSecondFrame = false;
																												
																												/* Camera setup */
																												Camera cam;
																												cameraInit(&cam, 0, -CAMERA_Y_OFFSET, -CAMERA_DISTANCE);
																												int16_t orbitAngle = PLAYER_START_ROTATION + 2048;  /* Start camera behind player */
																												
																												/* Track previous button state for edge detection */
																												uint16_t prevButtons = 0;
																												
																												/* Background flash effect */
																												int bgFlash = 0;
																												
																												/* Scene state management */
																												GameState gameState = STATE_TITLE;
																												GameState prePauseState = STATE_EXTERIOR; /* State before pausing */
																												int currentHouseIndex = -1;        /* Which house we're inside (-1 = none) */
																												int fadeAlpha = 0;                 /* Current fade level (0-255) */
																												int fadeHoldCounter = 0;           /* Frames to hold on black */
																												int32_t entryPosX = 0;             /* Position when entering house */
																												int32_t entryPosZ = 0;
																												int16_t entryFacing = 0;           /* Facing when entering house */
																												bool transitionToInterior = false; /* True = fading to interior, false = to exterior */
																												int frameCounter = 0;              /* For pause map blink animation */
																												
																												/* Intro text state */
																												int introCharCount = 0;            /* Characters to display (typewriter effect) */
																												bool introTextComplete = false;    /* True when all text displayed */

																												/* Day intro state */
																												int dayIntroTimer = 0;             /* Frames remaining for day intro display */
																												int currentDay = 1;                /* Current day number */

																												/* Mom/delivery state */
																												bool talkedToMom = false;          /* Has player talked to mom today */
																												bool hasFood = false;              /* Is player carrying food box */
																												int targetHouseIndex = -1;         /* Which house to deliver to (-1 = none) */
																												bool foodBoxSpawned = false;       /* Has food box been spawned in restaurant */
																												int32_t foodBoxX = 100 << 12;      /* Food box position in restaurant */
																												int32_t foodBoxZ = -100 << 12;

																												/* Dialog state */
																												const char *currentDialog = NULL;  /* Current dialog text to display */
																												int dialogCharCount = 0;           /* Characters displayed (typewriter) */
																												bool dialogComplete = false;       /* True when dialog fully displayed */


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
																															/* Normal mode: camera-relative movement */
																															/* Character turns toward target direction while moving forward
																															* (only walks if roughly aligned with target, otherwise turns in place) */
																															
																															/* D-pad left/right rotate camera */
																															if (pad.buttons & PAD_LEFT) {
																																orbitAngle -= PLAYER_TURN_SPEED;  /* Rotate camera left */
																															}
																															if (pad.buttons & PAD_RIGHT) {
																																orbitAngle += PLAYER_TURN_SPEED;  /* Rotate camera right */
																															}
																															
																															/* D-pad up: move forward in camera direction */
																															if (pad.buttons & PAD_UP) {
																																/* Turn character to face camera forward direction */
																																int16_t cameraForward = orbitAngle + 2048;  /* Opposite of where camera is */
																																int16_t diff = cameraForward - player.facing;
																																while (diff > 2048) diff -= 4096;
																																while (diff < -2048) diff += 4096;
																																
																																/* Turn toward target */
																																if (diff > ROTATION_THRESHOLD) moveX = 1;
																																else if (diff < -ROTATION_THRESHOLD) moveX = -1;
																																
																																/* Walk forward if roughly aligned (within ~90°) */
																																int16_t absDiff = (diff < 0) ? -diff : diff;
																																if (absDiff < 1024) moveZ = 1;
																															}
																															
																															/* D-pad down: turn toward camera and walk */
																															if (pad.buttons & PAD_DOWN) {
																																/* Turn character to face camera (walk toward it) */
																																int16_t diff = orbitAngle - player.facing;
																																while (diff > 2048) diff -= 4096;
																																while (diff < -2048) diff += 4096;
																																
																																/* Turn toward target */
																																if (diff > ROTATION_THRESHOLD) moveX = 1;
																																else if (diff < -ROTATION_THRESHOLD) moveX = -1;
																																
																																/* Walk forward if roughly aligned (within ~90°) */
																																int16_t absDiff = (diff < 0) ? -diff : diff;
																																if (absDiff < 1024) moveZ = 1;
																															}
																															
																															/* L1/R1: strafe right/left (no camera rotation) */
																															if (pad.buttons & PAD_L1) {
																																/* Turn character to face right of camera */
																																int16_t cameraRight = orbitAngle + 2048 - 1024;  /* 90° right of camera forward */
																																int16_t diff = cameraRight - player.facing;
																																while (diff > 2048) diff -= 4096;
																																while (diff < -2048) diff += 4096;
																																
																																if (diff > ROTATION_THRESHOLD) moveX = 1;
																																else if (diff < -ROTATION_THRESHOLD) moveX = -1;
																																
																																int16_t absDiff = (diff < 0) ? -diff : diff;
																																if (absDiff < 1024) moveZ = 1;
																															}
																															if (pad.buttons & PAD_R1) {
																																/* Turn character to face left of camera */
																																int16_t cameraLeft = orbitAngle + 2048 + 1024;  /* 90° left of camera forward */
																																int16_t diff = cameraLeft - player.facing;
																																while (diff > 2048) diff -= 4096;
																																while (diff < -2048) diff += 4096;
																																
																																if (diff > ROTATION_THRESHOLD) moveX = 1;
																																else if (diff < -ROTATION_THRESHOLD) moveX = -1;
																																
																																int16_t absDiff = (diff < 0) ? -diff : diff;
																																if (absDiff < 1024) moveZ = 1;
																															}
																															
																															/* Analog stick input */
																															if (pad.isAnalog) {
																																int stickX = (int)pad.leftX - 0x80;
																																int stickY = (int)pad.leftY - 0x80;
																																
																																/* X axis: rotate camera */
																																if (stickX > ANALOG_DEADZONE) orbitAngle += PLAYER_TURN_SPEED;
																																else if (stickX < -ANALOG_DEADZONE) orbitAngle -= PLAYER_TURN_SPEED;
																																
																																/* Y axis: up = forward in camera direction, down = toward camera */
																																if (stickY < -ANALOG_DEADZONE) {
																																	int16_t cameraForward = orbitAngle + 2048;
																																	int16_t diff = cameraForward - player.facing;
																																	while (diff > 2048) diff -= 4096;
																																	while (diff < -2048) diff += 4096;
																																	if (diff > ROTATION_THRESHOLD) moveX = 1;
																																	else if (diff < -ROTATION_THRESHOLD) moveX = -1;
																																	int16_t absDiff = (diff < 0) ? -diff : diff;
																																	if (absDiff < 1024) moveZ = 1;
																																} else if (stickY > ANALOG_DEADZONE) {
																																	int16_t diff = orbitAngle - player.facing;
																																	while (diff > 2048) diff -= 4096;
																																	while (diff < -2048) diff += 4096;
																																	if (diff > ROTATION_THRESHOLD) moveX = 1;
																																	else if (diff < -ROTATION_THRESHOLD) moveX = -1;
																																	int16_t absDiff = (diff < 0) ? -diff : diff;
																																	if (absDiff < 1024) moveZ = 1;
																																}
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
																																player.y = PLAYER_Y_OFFSET << 12;
																																player.z = 0;
																																player.facing = 0;
																																player.isWalking = false;
																															} else {
																																/* Restore player to entry position, facing away from door */
																																player.x = entryPosX;
																																player.z = entryPosZ;
																																player.facing = entryFacing + 2048;  /* 180° turn to face outward */
																																orbitAngle = player.facing + 2048;   /* Camera behind player */
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
																													} else if (gameState == STATE_DAY_INTRO) {
																														/* Day intro countdown */
																														dayIntroTimer--;
																														if (dayIntroTimer <= 0) {
																															gameState = STATE_INTERIOR;
																														}
																													} else if (gameState == STATE_DIALOG) {
																														/* Dialog typewriter effect */
																														if (currentDialog && !dialogComplete) {
																															if (frameCounter % 2 == 0) {
																																dialogCharCount++;
																															}
																															int dialogLen = 0;
																															const char *p = currentDialog;
																															while (*p++) dialogLen++;
																															if (dialogCharCount >= dialogLen) {
																																dialogComplete = true;
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
																														
																														/* Check all collision types (including restaurant) */
																														bool hasCollision =
																														checkAllHouseCollisions(newWorldX, newWorldZ, PLAYER_COLLISION_RADIUS, houses, NUM_HOUSES) ||
																														checkHouseCollision(newWorldX, newWorldZ, PLAYER_COLLISION_RADIUS, &restaurant) ||
																														checkAllTreeCollisions(newWorldX, newWorldZ, PLAYER_COLLISION_RADIUS, trees, NUM_MAP_TREES) ||
																														checkAllFenceCollisions(newWorldX, newWorldZ, PLAYER_COLLISION_RADIUS);
																														
																														if (hasCollision) {
																															/* Collision detected - try sliding along walls */
																															/* Try moving only in X (keep old Z) */
																															bool canMoveX = !(
																																checkAllHouseCollisions(newWorldX, oldWorldZ, PLAYER_COLLISION_RADIUS, houses, NUM_HOUSES) ||
																																checkHouseCollision(newWorldX, oldWorldZ, PLAYER_COLLISION_RADIUS, &restaurant) ||
																																checkAllTreeCollisions(newWorldX, oldWorldZ, PLAYER_COLLISION_RADIUS, trees, NUM_MAP_TREES) ||
																																checkAllFenceCollisions(newWorldX, oldWorldZ, PLAYER_COLLISION_RADIUS));
																																/* Try moving only in Z (keep old X) */
																																bool canMoveZ = !(
																																	checkAllHouseCollisions(oldWorldX, newWorldZ, PLAYER_COLLISION_RADIUS, houses, NUM_HOUSES) ||
																																	checkHouseCollision(oldWorldX, newWorldZ, PLAYER_COLLISION_RADIUS, &restaurant) ||
																																	checkAllTreeCollisions(oldWorldX, newWorldZ, PLAYER_COLLISION_RADIUS, trees, NUM_MAP_TREES) ||
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
																																/* Clamp player to interior floor bounds (restaurant has custom size) */
																																int32_t floorHalfX, floorHalfZ;
																																if (currentHouseIndex == NUM_HOUSES) {
																																	/* Restaurant interior */
																																	floorHalfX = RESTAURANT_INT_FLOOR_HALF_X;
																																	floorHalfZ = RESTAURANT_INT_FLOOR_HALF_Z;
																																} else {
																																	/* House interior */
																																	floorHalfX = INTERIOR_FLOOR_HALF_X;
																																	floorHalfZ = INTERIOR_FLOOR_HALF_Z;
																																}
																																
																																int32_t minX = -floorHalfX << 12;
																																int32_t maxX = floorHalfX << 12;
																																int32_t minZ = -floorHalfZ << 12;
																																int32_t maxZ = floorHalfZ << 12;
																																
																																if (player.x < minX) player.x = minX;
																																if (player.x > maxX) player.x = maxX;
																																if (player.z < minZ) player.z = minZ;
																																if (player.z > maxZ) player.z = maxZ;
																															}

																															/* Update mom in restaurant interior */
																															bool nearMom = false;
																															if (gameState == STATE_INTERIOR && currentHouseIndex == NUM_HOUSES) {
																																/* Calculate distance to mom */
																																int32_t momDistX = (player.x >> 12) - MOM_POS_X;
																																int32_t momDistZ = (player.z >> 12) - MOM_POS_Z;
																																int32_t momDistSq = momDistX * momDistX + momDistZ * momDistZ;
																																nearMom = (momDistSq < INTERACT_RADIUS * INTERACT_RADIUS) && !talkedToMom;

																																/* Make mom turn to face player when in talk range */
																																if (nearMom) {
																																	/* Calculate angle from mom to player (iatan2 returns PS1 angle units) */
																																	int16_t angleToPlayer = (int16_t)iatan2(momDistX, momDistZ);
																																	/* Smoothly turn toward player */
																																	int16_t angleDiff = angleToPlayer - mom.facing;
																																	while (angleDiff > 2048) angleDiff -= 4096;
																																	while (angleDiff < -2048) angleDiff += 4096;
																																	if (angleDiff > PLAYER_TURN_SPEED) mom.facing += PLAYER_TURN_SPEED;
																																	else if (angleDiff < -PLAYER_TURN_SPEED) mom.facing -= PLAYER_TURN_SPEED;
																																	else mom.facing = angleToPlayer;
																																}

																																/* Update mom idle animation (no movement) */
																																updateCharacter(&mom, 0, 0, deltaTime);
																															}

																															/* Check if near food box in restaurant */
							bool nearFoodBox = false;
							if (gameState == STATE_INTERIOR && currentHouseIndex == NUM_HOUSES && foodBoxSpawned && !hasFood) {
								int32_t foodDistX = (player.x >> 12) - FOOD_BOX_POS_X;
								int32_t foodDistZ = (player.z >> 12) - FOOD_BOX_POS_Z;
								int32_t foodDistSq = foodDistX * foodDistX + foodDistZ * foodDistZ;
								nearFoodBox = (foodDistSq < INTERACT_RADIUS * INTERACT_RADIUS);
							}

							/* Check door triggers (use current position after collision response) */
																															int triggeredDoor = -1;
																															bool atInteriorExit = false;
																															
																															if (gameState == STATE_EXTERIOR) {
																																triggeredDoor = checkDoorTrigger(player.x >> 12, player.z >> 12,
																																	houses, NUM_HOUSES);
																																	/* Also check restaurant door (use NUM_HOUSES as special index) */
																																	if (triggeredDoor < 0 && isAtBuildingDoor(player.x >> 12, player.z >> 12, &restaurant)) {
																																		triggeredDoor = NUM_HOUSES;  /* Special index for restaurant */
																																	}
																																} else if (gameState == STATE_INTERIOR && currentHouseIndex >= 0) {
																																	/* Check if player is at interior exit door using per-house settings */
																																	int32_t playerLocalX = player.x >> 12;
																																	int32_t playerLocalZ = player.z >> 12;
																																	
																																	/* Get per-house/restaurant door settings */
																																	int32_t doorX, doorZ, doorSizeX, doorSizeZ;
																																	if (currentHouseIndex == NUM_HOUSES) {
																																		/* Restaurant */
																																		doorX = RESTAURANT_INT_DOOR_X; doorZ = RESTAURANT_INT_DOOR_Z;
																																		doorSizeX = RESTAURANT_INT_DOOR_SIZE_X; doorSizeZ = RESTAURANT_INT_DOOR_SIZE_Z;
																																	} else {
																																		switch (currentHouseIndex % 3) {
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
																																	}
																																	
																																	/* Check if player is at interior exit door */
																																	if (playerLocalX >= doorX - doorSizeX && playerLocalX <= doorX + doorSizeX &&
																																		playerLocalZ >= doorZ - doorSizeZ && playerLocalZ <= doorZ + doorSizeZ) {
																																			atInteriorExit = true;
																																		}
																																	}
																																	
																																	/* X button handling depends on game state */
																																	if ((pad.buttons & PAD_X) && !(prevButtons & PAD_X)) {
																																		if (gameState == STATE_INTRO_1) {
																																			if (!introTextComplete) {
																																				/* First press: complete text instantly */
																																				introTextComplete = true;
																																				introCharCount = 9999;  /* Large number to show all */
																																			} else {
																																				/* Second press: go to next intro */
																																				gameState = STATE_INTRO_2;
																																				introCharCount = 0;
																																				introTextComplete = false;
																																			}
																																		} else if (gameState == STATE_INTRO_2) {
																																			if (!introTextComplete) {
																																				/* First press: complete text instantly */
																																				introTextComplete = true;
																																				introCharCount = 9999;  /* Large number to show all */
																																			} else {
																																				/* Second press: go to day intro */
																																				gameState = STATE_DAY_INTRO;
																																				dayIntroTimer = DAY_INTRO_DURATION;
																																				currentHouseIndex = NUM_HOUSES;
																																				player.x = 0;
																																				player.y = PLAYER_Y_OFFSET << 12;
																																				player.z = 0;
																																				player.facing = 0;
																																				player.isWalking = false;
																																				/* Set entry position to restaurant door for when player exits */
																																				entryPosX = (PLAYER_SPAWN_X + PLAYER_SPAWN_OFFSET_X) << 12;
																																				entryPosZ = (PLAYER_SPAWN_Z + PLAYER_SPAWN_OFFSET_Z) << 12;
																																				entryFacing = 2048;  /* Facing into restaurant (will turn 180 on exit) */
																																			}
																																		} else if (gameState == STATE_DAY_INTRO) {
																																			/* Skip day intro */
																																			gameState = STATE_INTERIOR;
																																		} else if (gameState == STATE_DIALOG) {
																																			/* Advance/close dialog */
																																			if (!dialogComplete) {
																																				dialogComplete = true;
																																				dialogCharCount = 9999;
																																			} else {
																																				gameState = STATE_INTERIOR;
																																				currentDialog = NULL;
																																				/* If just talked to mom, spawn food box */
																																				if (talkedToMom && !foodBoxSpawned) {
																																					foodBoxSpawned = true;
																																					/* Pick random target house */
																																					targetHouseIndex = frameCounter % NUM_HOUSES;
																																				}
																																			}
																																		} else if (gameState == STATE_EXTERIOR && triggeredDoor >= 0) {
																																			/* Start transition to interior */
																																			entryPosX = player.x;
																																			entryPosZ = player.z;
																																			entryFacing = player.facing;
																																			currentHouseIndex = triggeredDoor;
																																			transitionToInterior = true;
																																			gameState = STATE_FADE_OUT;
																																			fadeAlpha = 0;
																																		} else if (gameState == STATE_INTERIOR && currentHouseIndex == NUM_HOUSES) {
																																			/* Restaurant: check mom/food interaction FIRST, then exit */
																																			bool handledInteraction = false;

																																			/* Check if near mom to talk */
																																			if (!talkedToMom) {
																																				int32_t momDistX = (player.x >> 12) - MOM_POS_X;
																																				int32_t momDistZ = (player.z >> 12) - MOM_POS_Z;
																																				int32_t momDistSq = momDistX * momDistX + momDistZ * momDistZ;
																																				if (momDistSq < INTERACT_RADIUS * INTERACT_RADIUS) {
																																					/* Talk to mom */
																																					talkedToMom = true;
																																					currentDialog = MOM_DIALOG;
																																					dialogCharCount = 0;
																																					dialogComplete = false;
																																					gameState = STATE_DIALOG;
																																					handledInteraction = true;
																																				}
																																			}

																																			/* Check if near food box to pick up */
																																			if (!handledInteraction && foodBoxSpawned && !hasFood) {
																																				int32_t foodDistX = (player.x >> 12) - FOOD_BOX_POS_X;
																																				int32_t foodDistZ = (player.z >> 12) - FOOD_BOX_POS_Z;
																																				int32_t foodDistSq = foodDistX * foodDistX + foodDistZ * foodDistZ;
																																				if (foodDistSq < INTERACT_RADIUS * INTERACT_RADIUS) {
																																					hasFood = true;
																																					player.isCarrying = true;
																																					handledInteraction = true;
																																				}
																																			}

																																			/* Check exit - only if no interaction happened */
																																			if (!handledInteraction && atInteriorExit) {
																																				if (!talkedToMom) {
																																					/* Show message - need to talk to mom first */
																																					currentDialog = NEED_TO_TALK_MSG;
																																					dialogCharCount = 0;
																																					dialogComplete = false;
																																					gameState = STATE_DIALOG;
																																				} else if (!hasFood) {
																																					/* Show message - need to pick up food first */
																																					currentDialog = NEED_FOOD_MSG;
																																					dialogCharCount = 0;
																																					dialogComplete = false;
																																					gameState = STATE_DIALOG;
																																				} else {
																																					/* Can leave - start transition to exterior */
																																					transitionToInterior = false;
																																					gameState = STATE_FADE_OUT;
																																					fadeAlpha = 0;
																																				}
																																			}
																																		} else if (gameState == STATE_INTERIOR && atInteriorExit) {
																																			/* Normal house exit - no requirements */
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
																																	
																																	/* Start button handling */
																																	if ((pad.buttons & PAD_START) && !(prevButtons & PAD_START)) {
																																		if (gameState == STATE_TITLE) {
																																			/* Start game - go to first intro */
																																			gameState = STATE_INTRO_1;
																																			introCharCount = 0;
																																			introTextComplete = false;
																																		} else if (gameState == STATE_INTRO_1) {
																																			if (!introTextComplete) {
																																				/* First press: complete text instantly */
																																				introTextComplete = true;
																																				introCharCount = 9999;
																																			} else {
																																				/* Second press: go to next intro */
																																				gameState = STATE_INTRO_2;
																																				introCharCount = 0;
																																				introTextComplete = false;
																																			}
																																		} else if (gameState == STATE_INTRO_2) {
																																			if (!introTextComplete) {
																																				/* First press: complete text instantly */
																																				introTextComplete = true;
																																				introCharCount = 9999;
																																			} else {
																																				/* Second press: go to day intro */
																																				gameState = STATE_DAY_INTRO;
																																				dayIntroTimer = DAY_INTRO_DURATION;
																																				currentHouseIndex = NUM_HOUSES;  /* Restaurant */
																																				player.x = 0;
																																				player.y = PLAYER_Y_OFFSET << 12;
																																				player.z = 0;
																																				player.facing = 0;
																																				player.isWalking = false;
																																				/* Set entry position to restaurant door for when player exits */
																																				entryPosX = (PLAYER_SPAWN_X + PLAYER_SPAWN_OFFSET_X) << 12;
																																				entryPosZ = (PLAYER_SPAWN_Z + PLAYER_SPAWN_OFFSET_Z) << 12;
																																				entryFacing = 2048;  /* Facing into restaurant (will turn 180 on exit) */
																																			}
																																		} else if (gameState == STATE_DAY_INTRO) {
																																			/* Skip day intro */
																																			gameState = STATE_INTERIOR;
																																		} else if (gameState == STATE_PAUSED) {
																																			/* Resume game */
																																			gameState = prePauseState;
																																		} else if (gameState == STATE_EXTERIOR || gameState == STATE_INTERIOR) {
																																			/* Pause game */
																																			prePauseState = gameState;
																																			gameState = STATE_PAUSED;
																																		}
																																	}
																																	
																																	prevButtons = pad.buttons;
																																	frameCounter++;
																																	
																																	/* Update CD-DA looping */
																																	// updateCDDA();
																																	
																																	/* Check if we're in menu/intro state */
																																	bool inMenuState = (gameState == STATE_TITLE || gameState == STATE_INTRO_1 || gameState == STATE_INTRO_2 || gameState == STATE_DAY_INTRO);

																																	if (inMenuState) {
																																		/* ============ TITLE / INTRO RENDERING ============ */
																																		
																																		/* Black background - use triangles to properly clear screen */
																																		ptr = allocatePacket(chain, ORDERING_TABLE_SIZE - 1, 4);
																																		ptr[0] = gp0_rgb(0, 0, 0) | gp0_triangle(false, false);
																																		ptr[1] = gp0_xy(0, 0);
																																		ptr[2] = gp0_xy(SCREEN_WIDTH, 0);
																																		ptr[3] = gp0_xy(SCREEN_WIDTH, SCREEN_HEIGHT);
																																		
																																		ptr = allocatePacket(chain, ORDERING_TABLE_SIZE - 1, 4);
																																		ptr[0] = gp0_rgb(0, 0, 0) | gp0_triangle(false, false);
																																		ptr[1] = gp0_xy(0, 0);
																																		ptr[2] = gp0_xy(SCREEN_WIDTH, SCREEN_HEIGHT);
																																		ptr[3] = gp0_xy(0, SCREEN_HEIGHT);
																																		
																																		if (gameState == STATE_TITLE) {
																																			/* Title screen */
																																			const char *title = "REVENANTS OF ELMORIA";
																																			const char *prompt = "Press [START] to play";
																																			
																																			/* Center the title (approx 5 pixels per char average) */
																																			int titleX = (SCREEN_WIDTH - 20 * 5) / 2;
																																			int titleY = 80;
																																			
																																			/* Draw title with shadow */
																																			printStringColorZ(chain, &font, titleX + 1, titleY + 1, title, 40, 20, 20, 1);
																																			printStringColorZ(chain, &font, titleX, titleY, title, 200, 180, 100, 0);
																																			
																																			/* Center the prompt */
																																			int promptX = (SCREEN_WIDTH - 21 * 5) / 2;
																																			int promptY = 160;
																																			
																																			/* Blink effect for prompt (toggle every 30 frames) */
																																			if ((frameCounter / 30) % 2 == 0) {
																																				printStringColorZ(chain, &font, promptX + 1, promptY + 1, prompt, 20, 20, 40, 1);
																																				printStringColorZ(chain, &font, promptX, promptY, prompt, 100, 150, 255, 0);
																																			}
																																		} else if (gameState == STATE_INTRO_1 || gameState == STATE_INTRO_2) {
																																			/* Intro text screens */
																																			const char *text = (gameState == STATE_INTRO_1) ? INTRO_QUOTE : INTRO_STORY;
																																			int textLen = 0;
																																			for (const char *p = text; *p; p++) textLen++;
																																			
																																			/* Update typewriter effect (advance every 2 frames for slower speed) */
																																			if (!introTextComplete && (frameCounter % 2 == 0)) {
																																				introCharCount += INTRO_TEXT_SPEED;
																																				if (introCharCount >= textLen) {
																																					introCharCount = textLen;
																																					introTextComplete = true;
																																				}
																																			}
																																			
																																			/* Create a temporary buffer with the displayed portion of text */
																																			char displayText[512];
																																			int i;
																																			for (i = 0; i < introCharCount && i < 511 && text[i]; i++) {
																																				displayText[i] = text[i];
																																			}
																																			displayText[i] = '\0';
																																			
																																			/* Draw text */
																																			int textX = 20;
																																			int textY = (gameState == STATE_INTRO_1) ? 60 : 30;
																																			
																																			printStringColorZ(chain, &font, textX + 1, textY + 1, displayText, 30, 30, 40, 1);
																																			printStringColorZ(chain, &font, textX, textY, displayText, 180, 180, 200, 0);
																																			
																																			/* Show blinking continue prompt only when text is complete */
																																			if (introTextComplete && (frameCounter / 30) % 2 == 0) {
																																				const char *continuePrompt = "[START] or [X] to continue";
																																				int promptX = (SCREEN_WIDTH - 26 * 5) / 2;
																																				int promptY = 220;
																																				printStringColorZ(chain, &font, promptX + 1, promptY + 1, continuePrompt, 20, 20, 40, 1);
																																				printStringColorZ(chain, &font, promptX, promptY, continuePrompt, 100, 150, 255, 0);
																																			}
																																		} else if (gameState == STATE_DAY_INTRO) {
																																			/* Day intro screen - "DAY X - Threat level Y" */
																																			char dayText[64];
																																			/* Build day string */
																																			dayText[0] = 'D'; dayText[1] = 'A'; dayText[2] = 'Y'; dayText[3] = ' ';
																																			dayText[4] = '0' + currentDay;
																																			dayText[5] = '\0';

																																			char threatText[32];
																																			threatText[0] = 'T'; threatText[1] = 'h'; threatText[2] = 'r'; threatText[3] = 'e';
																																			threatText[4] = 'a'; threatText[5] = 't'; threatText[6] = ' '; threatText[7] = 'l';
																																			threatText[8] = 'e'; threatText[9] = 'v'; threatText[10] = 'e'; threatText[11] = 'l';
																																			threatText[12] = ' '; threatText[13] = 'L'; threatText[14] = 'o'; threatText[15] = 'w';
																																			threatText[16] = '\0';

																																			/* Center the day text */
																																			int dayX = (SCREEN_WIDTH - 5 * 8) / 2;
																																			int dayY = 90;

																																			/* Draw day with shadow */
																																			printStringColorZ(chain, &font, dayX + 1, dayY + 1, dayText, 40, 20, 20, 1);
																																			printStringColorZ(chain, &font, dayX, dayY, dayText, 255, 220, 100, 0);

																																			/* Threat level below */
																																			int threatX = (SCREEN_WIDTH - 16 * 5) / 2;
																																			int threatY = 120;
																																			printStringColorZ(chain, &font, threatX + 1, threatY + 1, threatText, 20, 40, 20, 1);
																																			printStringColorZ(chain, &font, threatX, threatY, threatText, 100, 200, 100, 0);
																																		}
																																	} else {
																																		/* ============ NORMAL GAME RENDERING ============ */
																																		
																																		/* Determine which scene to render based on state and transition direction
																																		* FADE_OUT: render the scene we're LEAVING (opposite of transition direction)
																																		* BLACK/FADE_IN: render the scene we're GOING TO (same as transition direction)
																																		* DIALOG: stay in interior if we were inside (currentHouseIndex >= 0) */
																																		bool renderInterior = (gameState == STATE_INTERIOR) ||
																																		(gameState == STATE_DIALOG && currentHouseIndex >= 0) ||  /* dialog in interior */
																																		(gameState == STATE_PAUSED && prePauseState == STATE_INTERIOR) ||  /* paused from interior */
																																		(gameState == STATE_FADE_OUT && !transitionToInterior) ||  /* leaving interior */
																																		((gameState == STATE_BLACK || gameState == STATE_FADE_IN) && transitionToInterior);  /* entering interior */
																																		
																																		/* Camera handling depends on scene being rendered */
																																		if (!renderInterior) {
																																			/* Camera orbit angle is controlled by D-pad left/right in input handling above */
																																			/* No auto-follow - player has direct camera control */
																																			
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
																																				/* Add pitch offset to look more downward (reduces affine texture distortion) */
																																				cameraAddPitch(&cam, CAMERA_PITCH_OFFSET);
																																			} else {
																																				/* Fixed camera for interior - looking at room center from fixed angle */
																																				/* Get per-house-type camera settings based on house's model type */
																																				int32_t interiorCamDist = INTERIOR_CAMERA_DISTANCE;
																																				int32_t interiorCamY = INTERIOR_CAMERA_Y_OFFSET;
																																				if (currentHouseIndex == NUM_HOUSES) {
																																					/* Restaurant interior */
																																					interiorCamDist = RESTAURANT_INT_CAM_DIST;
																																					interiorCamY = RESTAURANT_INT_CAM_Y;
																																				} else if (currentHouseIndex >= 0 && currentHouseIndex < NUM_HOUSES) {
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
																																					
																																					/* Draw the current house/restaurant interior (model centered at origin) */
																																					if (currentHouseIndex >= 0) {
																																						int16_t interiorRotation = 0;
																																						int32_t modelOffsetX = 0, modelOffsetZ = 0;
																																						int32_t doorX = 0, doorZ = 0, doorSizeX = 0, doorSizeZ = 0;
																																						int32_t floorHalfX = 0, floorHalfZ = 0;
																																						Model *interiorModel;
																																						
																																						if (currentHouseIndex == NUM_HOUSES) {
																																							/* Restaurant interior */
																																							interiorRotation = RESTAURANT_INT_ROTATION;
																																							modelOffsetX = RESTAURANT_INT_MODEL_X;
																																							modelOffsetZ = RESTAURANT_INT_MODEL_Z;
																																							doorX = RESTAURANT_INT_DOOR_X;
																																							doorZ = RESTAURANT_INT_DOOR_Z;
																																							doorSizeX = RESTAURANT_INT_DOOR_SIZE_X;
																																							doorSizeZ = RESTAURANT_INT_DOOR_SIZE_Z;
																																							floorHalfX = RESTAURANT_INT_FLOOR_HALF_X;
																																							floorHalfZ = RESTAURANT_INT_FLOOR_HALF_Z;
																																							interiorModel = &restaurantInterior;
																																						} else {
																																							/* Get per-house-type interior settings based on house's model type */
																																							int modelType = mapHouses[currentHouseIndex].modelType % 3;
																																							/* Houses use shared floor bounds */
																																							floorHalfX = INTERIOR_FLOOR_HALF_X;
																																							floorHalfZ = INTERIOR_FLOOR_HALF_Z;
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
																																							interiorModel = &interiorModels[modelType];
																																						}
																																						
																																						/* Create temporary house struct with interior model */
																																						House interiorHouse;
																																						interiorHouse.model = *interiorModel;
																																						interiorHouse.x = modelOffsetX;
																																						interiorHouse.y = FLOOR_Y;
																																						interiorHouse.z = modelOffsetZ;
																																						interiorHouse.rotation = interiorRotation;
																																						drawHouse(chain, &interiorHouse, &cam);
																																						
																																						#if DEBUG_DRAW_COLLISION
																																						/* Draw interior debug: floor bounds and door trigger */
																																						drawInteriorDebug(chain, &cam, doorX, doorZ, doorSizeX, doorSizeZ, floorHalfX, floorHalfZ);
																																						#endif
																																					}
																																					
																																					/* Draw character in interior */
																																					drawCharacter(chain, &player, &cam);
																																					/* Draw food box if player is carrying */
																																					if (hasFood) {
																																						drawCharacterItem(chain, &player, &foodBoxModel, &cam,
																																							CARRY_BOX_OFFSET_Y, CARRY_BOX_OFFSET_Z,
																																							CARRY_BOX_BOB_AMOUNT, FOOD_BOX_SCALE);
																																					}

																																					/* Draw mom in restaurant only */
																																					if (currentHouseIndex == NUM_HOUSES) {
																																						drawCharacter(chain, &mom, &cam);
																																						/* Draw food box on table when spawned but not picked up */
																																						if (foodBoxSpawned && !hasFood) {
																																							drawWorldItem(chain, &foodBoxModel, &cam,
								FOOD_BOX_POS_X, FOOD_BOX_TABLE_Y, FOOD_BOX_POS_Z,
								0, FOOD_BOX_SCALE);
																																						}
																																					}

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
																																					/* Draw food box if player is carrying */
																																					if (hasFood) {
																																						drawCharacterItem(chain, &player, &foodBoxModel, &cam,
																																							CARRY_BOX_OFFSET_Y, CARRY_BOX_OFFSET_Z,
																																							CARRY_BOX_BOB_AMOUNT, FOOD_BOX_SCALE);
																																					}
																																					for (int i = 0; i < NUM_HOUSES; i++) {
																																						drawHouse(chain, &houses[i], &cam);
																																					}
																																					/* Draw restaurant */
																																					drawHouse(chain, &restaurant, &cam);
																																					
																																					/* 3. Draw trees */
																																					setupTreeBatch(&cam);
																																					for (int i = 0; i < NUM_MAP_TREES; i++) {
																																						drawTree(chain, &trees[i], &cam);
																																					}
																																					
																																					/* 4. Draw fence posts (batched - set up GTE once) */
																																					setupFenceBatch(&cam);
																																					for (int i = 0; i < NUM_FENCE_POSTS; i++) {
																																						drawFencePost(chain, &mapFencePosts[i], &cam);
																																					}
																																					
																																					#if DEBUG_DRAW_COLLISION
																																					/* 5. Draw collision debug wireframes */
																																					drawAllCollisionDebug(chain, houses, NUM_HOUSES, &cam);
																																					/* Also draw restaurant collision */
																																					drawHouseCollisionDebug(chain, &restaurant, &cam);
																																					/* 6. Draw door trigger wireframes (red) */
																																					drawAllDoorTriggersDebug(chain, houses, NUM_HOUSES, &cam);
																																					/* Also draw restaurant door trigger */
																																					drawDoorTriggerDebug(chain, &restaurant, &cam);
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
																																				
																																				/* Check if we're in pause state */
																																				if (gameState == STATE_PAUSED) {
																																					/* Draw dark blue background for pause screen */
																																					ptr = allocatePacket(chain, ORDERING_TABLE_SIZE - 1, 3);
																																					ptr[0] = gp0_rgb(15, 20, 35) | gp0_rectangle(false, false, false);
																																					ptr[1] = gp0_xy(0, 0);
																																					ptr[2] = gp0_xy(SCREEN_WIDTH, SCREEN_HEIGHT);
																																					
																																					/* Draw the pause map - use entry position when inside a house */
																																					int32_t mapX = (prePauseState == STATE_INTERIOR) ? (entryPosX >> 12) : (player.x >> 12);
																																					int32_t mapZ = (prePauseState == STATE_INTERIOR) ? (entryPosZ >> 12) : (player.z >> 12);
																																					drawPauseMap(chain, mapX, mapZ, player.facing, frameCounter);
																																					
																																					/* Draw "PAUSED" text */
																																					printStringColor(chain, &font, 130, 8, "PAUSED", 255, 255, 100);
																																					printStringColor(chain, &font, 100, 220, "press START to resume", 150, 150, 150);
																																				} else {
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
																																				}
																																				
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
																																						/* Get building name */
																																						char addrText[20];
																																						int addrX;
																																						if (triggeredDoor == NUM_HOUSES) {
																																							/* Restaurant */
																																							sprintf(addrText, "Restaurant");
																																							addrX = 115;  /* Centered for "Restaurant" */
																																						} else {
																																							/* House - show address */
																																							uint16_t houseAddr = mapHouses[triggeredDoor].address;
																																							sprintf(addrText, "House %d", houseAddr);
																																							addrX = 125;  /* Roughly centered */
																																						}
																																						
																																						const char *doorPrompt = "press [X] to enter";
																																						int promptX = 110;  /* Roughly centered for this string */
																																						int promptY = 190; /* Near bottom of screen */
																																						
																																						/* Draw building name first (above prompt) - shadow at OT 1, text at OT 0 */
																																						printStringColorZ(chain, &font, addrX + 1, promptY - 9, addrText, 20, 20, 40, 1);
																																						printStringColorZ(chain, &font, addrX, promptY - 10, addrText, 255, 220, 100, 0);
																																						
																																						/* Draw door prompt - shadow at OT 1 (behind), text at OT 0 (front) */
																																						printStringColorZ(chain, &font, promptX + 1, promptY + 1, doorPrompt, 20, 20, 40, 1);
																																						printStringColorZ(chain, &font, promptX, promptY, doorPrompt, 100, 150, 255, 0);
																																					} else if (gameState == STATE_INTERIOR && atInteriorExit) {
																																						const char *doorPrompt = "press [X] to leave";
																																						int promptX = 110;  /* Roughly centered */
																																						int promptY = 200;
																																						printStringColorZ(chain, &font, promptX + 1, promptY + 1, doorPrompt, 20, 20, 40, 1);
																																						printStringColorZ(chain, &font, promptX, promptY, doorPrompt, 100, 150, 255, 0);
																																					} else if (gameState == STATE_INTERIOR && nearMom && !talkedToMom) {
																																						const char *talkPrompt = "press [X] to talk";
																																						int promptX = 115;  /* Roughly centered */
																																						int promptY = 200;
																																						printStringColorZ(chain, &font, promptX + 1, promptY + 1, talkPrompt, 20, 20, 40, 1);
																																						printStringColorZ(chain, &font, promptX, promptY, talkPrompt, 100, 150, 255, 0);
					} else if (gameState == STATE_INTERIOR && nearFoodBox) {
						const char *pickupPrompt = "press [X] to pick up food";
						int promptX = 90;
						int promptY = 200;
						printStringColorZ(chain, &font, promptX + 1, promptY + 1, pickupPrompt, 20, 20, 40, 1);
						printStringColorZ(chain, &font, promptX, promptY, pickupPrompt, 100, 150, 255, 0);
																																					}

																																					/* Draw dialog box when in dialog state */
																																					if (gameState == STATE_DIALOG && currentDialog != NULL) {
																																						/* Dialog box dimensions - at top of screen */
																																						int boxX = 20;
																																						int boxY = 20;
																																						int boxW = SCREEN_WIDTH - 40;
																																						int boxH = 60;

																																						/* Draw dark background box at OT 2 (behind text) */
																																						/* Use two triangles to form a quad */
																																						ptr = allocatePacket(chain, 2, 4);
																																						ptr[0] = gp0_rgb(20, 15, 40) | gp0_triangle(false, false);
																																						ptr[1] = gp0_xy(boxX, boxY);
																																						ptr[2] = gp0_xy(boxX + boxW, boxY);
																																						ptr[3] = gp0_xy(boxX, boxY + boxH);

																																						ptr = allocatePacket(chain, 2, 4);
																																						ptr[0] = gp0_rgb(20, 15, 40) | gp0_triangle(false, false);
																																						ptr[1] = gp0_xy(boxX + boxW, boxY);
																																						ptr[2] = gp0_xy(boxX + boxW, boxY + boxH);
																																						ptr[3] = gp0_xy(boxX, boxY + boxH);

																																						/* Draw border (4 lines) at OT 1 */
																																						ptr = allocatePacket(chain, 1, 3);
																																						ptr[0] = gp0_rgb(100, 80, 150) | 0x40000000;  /* Line command */
																																						ptr[1] = gp0_xy(boxX, boxY);
																																						ptr[2] = gp0_xy(boxX + boxW, boxY);

																																						ptr = allocatePacket(chain, 1, 3);
																																						ptr[0] = gp0_rgb(100, 80, 150) | 0x40000000;
																																						ptr[1] = gp0_xy(boxX + boxW, boxY);
																																						ptr[2] = gp0_xy(boxX + boxW, boxY + boxH);

																																						ptr = allocatePacket(chain, 1, 3);
																																						ptr[0] = gp0_rgb(100, 80, 150) | 0x40000000;
																																						ptr[1] = gp0_xy(boxX + boxW, boxY + boxH);
																																						ptr[2] = gp0_xy(boxX, boxY + boxH);

																																						ptr = allocatePacket(chain, 1, 3);
																																						ptr[0] = gp0_rgb(100, 80, 150) | 0x40000000;
																																						ptr[1] = gp0_xy(boxX, boxY + boxH);
																																						ptr[2] = gp0_xy(boxX, boxY);

																																						/* Copy dialog text up to dialogCharCount for typewriter effect */
																																						static char dialogBuffer[256];
																																						int i = 0;
																																						const char *src = currentDialog;
																																						while (*src && i < dialogCharCount && i < 255) {
																																							dialogBuffer[i++] = *src++;
																																						}
																																						dialogBuffer[i] = '\0';

																																						/* Draw dialog text (no shadow - box provides contrast) */
																																						int textX = boxX + 5;
																																						int textY = boxY + 5;
																																						printStringColor(chain, &font, textX, textY, dialogBuffer, 220, 200, 255);

																																						/* Show continue prompt below box when text is complete */
																																						if (dialogComplete) {
																																							const char *continueText = "[X] continue";
																																							int contX = boxX + boxW - 70;
																																							int contY = boxY + boxH + 5;
																																							if ((frameCounter / 20) % 2 == 0) {
																																								printStringColor(chain, &font, contX, contY, continueText, 150, 200, 255);
																																							}
																																						}
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
																																					
																																				} /* End of else (normal game rendering) */
																																				
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
																																		