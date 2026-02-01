/*
 * Collision Detection - Circle vs AABB and door trigger checks
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "game_types.h"
#include "world_data.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Check if a circle (player) collides with a single AABB */
bool checkCircleBoxCollision(int32_t circleX, int32_t circleZ, int32_t radius,
	int32_t boxMinX, int32_t boxMinZ,
	int32_t boxMaxX, int32_t boxMaxZ);

/* Check if player collides with a house's collision boxes */
bool checkHouseCollision(int32_t playerX, int32_t playerZ, int32_t radius,
	const House *house);

/* Check collision against all houses */
bool checkAllHouseCollisions(int32_t playerX, int32_t playerZ, int32_t radius,
	const House *houses, int numHouses);

/* Check if player is inside a house's door trigger zone
 * Returns index of triggered door (0-based), or -1 if no trigger */
int checkDoorTrigger(int32_t playerX, int32_t playerZ,
	const House *houses, int numHouses);

/* Check if player is at a single building's door (for restaurant) */
bool isAtBuildingDoor(int32_t playerX, int32_t playerZ, const House *building);

/* Check if player collides with a fence post (box-circle collision) */
bool checkFencePostCollision(int32_t playerX, int32_t playerZ, int32_t radius,
	const FencePost *post);

/* Check if player collides with a tree (circle-circle collision) */
bool checkTreeCollision(int32_t playerX, int32_t playerZ, int32_t playerRadius,
	const Tree *tree);

/* Check collision against all trees */
bool checkAllTreeCollisions(int32_t playerX, int32_t playerZ, int32_t radius,
	const Tree *trees, int numTrees);

/* Check collision against all fence posts */
bool checkAllFenceCollisions(int32_t playerX, int32_t playerZ, int32_t radius);

#ifdef __cplusplus
}
#endif
