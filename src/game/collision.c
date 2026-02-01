/*
 * Collision Detection Implementation
 */

#include "collision.h"
#include "game_config.h"

/* Check if a circle (player) collides with a single AABB
 * Returns true if collision detected */
bool checkCircleBoxCollision(int32_t circleX, int32_t circleZ, int32_t radius,
	int32_t boxMinX, int32_t boxMinZ,
	int32_t boxMaxX, int32_t boxMaxZ)
{
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
bool checkHouseCollision(int32_t playerX, int32_t playerZ, int32_t radius,
	const House *house)
{
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
bool checkAllHouseCollisions(int32_t playerX, int32_t playerZ, int32_t radius,
	const House *houses, int numHouses)
{
	for (int i = 0; i < numHouses; i++) {
		if (checkHouseCollision(playerX, playerZ, radius, &houses[i])) {
			return true;
		}
	}
	return false;
}

/* Check if player is inside a house's door trigger zone
 * Returns index of triggered door (0-based), or -1 if no trigger */
int checkDoorTrigger(int32_t playerX, int32_t playerZ,
	const House *houses, int numHouses)
{
	for (int i = 0; i < numHouses; i++) {
		const House *house = &houses[i];
		const DoorTrigger *door = &house->door;

		/* Scale door offset and size by HOUSE_SCALE */
		int32_t scaledOffsetX = (door->offsetX * HOUSE_SCALE) >> 12;
		int32_t scaledOffsetZ = (door->offsetZ * HOUSE_SCALE) >> 12;
		int32_t scaledSizeX = (door->sizeX * HOUSE_SCALE) >> 12;
		int32_t scaledSizeZ = (door->sizeZ * HOUSE_SCALE) >> 12;

		/* Rotate door offset based on house rotation (90 degree intervals)
		 * Normalize rotation to 0-4095 range first */
		int16_t rot = house->rotation;
		while (rot < 0) rot += 4096;
		while (rot >= 4096) rot -= 4096;

		int32_t worldOffsetX, worldOffsetZ;
		if (rot < 512) {
			/* ~0 degrees */
			worldOffsetX = scaledOffsetX;
			worldOffsetZ = scaledOffsetZ;
		} else if (rot < 1536) {
			/* ~90 degrees */
			worldOffsetX = scaledOffsetZ;
			worldOffsetZ = -scaledOffsetX;
		} else if (rot < 2560) {
			/* ~180 degrees */
			worldOffsetX = -scaledOffsetX;
			worldOffsetZ = -scaledOffsetZ;
		} else if (rot < 3584) {
			/* ~270 degrees */
			worldOffsetX = -scaledOffsetZ;
			worldOffsetZ = scaledOffsetX;
		} else {
			/* ~360 degrees (wraps to ~0) */
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
bool isAtBuildingDoor(int32_t playerX, int32_t playerZ, const House *building)
{
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

/* Check if player collides with a fence post (box-circle collision) */
bool checkFencePostCollision(int32_t playerX, int32_t playerZ, int32_t radius,
	const FencePost *post)
{
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
bool checkTreeCollision(int32_t playerX, int32_t playerZ, int32_t playerRadius,
	const Tree *tree)
{
	int32_t dx = playerX - tree->x;
	int32_t dz = playerZ - tree->z;
	int32_t combinedRadius = playerRadius + TREE_COLLISION_RADIUS;
	int64_t distSq = (int64_t)dx * dx + (int64_t)dz * dz;
	int64_t radiusSq = (int64_t)combinedRadius * combinedRadius;
	return distSq < radiusSq;
}

/* Check collision against all trees */
bool checkAllTreeCollisions(int32_t playerX, int32_t playerZ, int32_t radius,
	const Tree *trees, int numTrees)
{
	for (int i = 0; i < numTrees; i++) {
		if (checkTreeCollision(playerX, playerZ, radius, &trees[i])) {
			return true;
		}
	}
	return false;
}

/* Check collision against all fence posts */
bool checkAllFenceCollisions(int32_t playerX, int32_t playerZ, int32_t radius)
{
	for (int i = 0; i < NUM_FENCE_POSTS; i++) {
		if (checkFencePostCollision(playerX, playerZ, radius, &mapFencePosts[i])) {
			return true;
		}
	}
	return false;
}
