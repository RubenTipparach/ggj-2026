/*
 * UI Implementation - HUD elements and pause map
 */

#include "ui.h"
#include "world_data.h"
#include "trig.h"
#include "font.h"
#include "ps1/gpucmd.h"
#include "game_config.h"

/* Draw the pause screen map view */
void drawPauseMap(DMAChain *chain, int32_t playerWorldX, int32_t playerWorldZ,
	int16_t playerFacing, int frameCounter,
	const Enforcer *enforcers, int numEnforcers)
{
	/* Map display area - centered on screen */
	const int mapScreenSize = 128;  /* Square map area */
	const int mapScreenX = (SCREEN_WIDTH - mapScreenSize) / 2;
	const int mapScreenY = (SCREEN_HEIGHT - mapScreenSize) / 2;

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

	/* Draw restaurant as orange square at OT index 1 */
	{
		int px = (PLAYER_SPAWN_X + MAP_WORLD_SIZE / 2) / MAP_SCALE;
		int pz = (MAP_WORLD_SIZE / 2 - PLAYER_SPAWN_Z) / MAP_SCALE;

		int screenX = mapScreenX + ((px * scale) >> 8) - 3;
		int screenY = mapScreenY + ((pz * scale) >> 8) - 3;

		ptr = allocatePacket(chain, 1, 3);
		ptr[0] = gp0_rgb(255, 180, 80) | gp0_rectangle(false, false, false);
		ptr[1] = gp0_xy(screenX, screenY);
		ptr[2] = gp0_xy(7, 7);
	}

	/* Draw player position as blinking yellow arrow at OT index 0 (on top) */
	if ((frameCounter / 15) % 2 == 0) {
		int playerPx = (playerWorldX + MAP_WORLD_SIZE / 2) / MAP_SCALE;
		int playerPz = (MAP_WORLD_SIZE / 2 - playerWorldZ) / MAP_SCALE;

		int centerX = mapScreenX + ((playerPx * scale) >> 8);
		int centerY = mapScreenY + ((playerPz * scale) >> 8);

		/* Direction vector from facing angle */
		int dirX = isin(playerFacing);
		int dirY = -icos(playerFacing);

		/* Arrow size in pixels */
		const int arrowFront = 5;
		const int arrowBack = 3;
		const int arrowSide = 3;

		/* Calculate arrow vertices */
		int tipX = centerX + ((dirX * arrowFront) >> 12);
		int tipY = centerY + ((dirY * arrowFront) >> 12);

		int backX = centerX - ((dirX * arrowBack) >> 12);
		int backY = centerY - ((dirY * arrowBack) >> 12);

		/* Perpendicular direction */
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

#if DEBUG_VISUAL_AGENTS
	/* Draw enforcers as red dots on the map */
	for (int i = 0; i < numEnforcers; i++) {
		if (!enforcers[i].isActive) continue;

		/* Convert enforcer world pos to map pixel */
		int32_t enfX = enforcers[i].x >> 12;  /* Fixed-point to world units */
		int32_t enfZ = enforcers[i].z >> 12;

		int px = (enfX + MAP_WORLD_SIZE / 2) / MAP_SCALE;
		int pz = (MAP_WORLD_SIZE / 2 - enfZ) / MAP_SCALE;

		int screenX = mapScreenX + ((px * scale) >> 8);
		int screenY = mapScreenY + ((pz * scale) >> 8);

		/* Color based on state: patrol=blue, alert=yellow, chase=red */
		uint8_t r, g, b;
		if (enforcers[i].state == ENFORCER_CHASE) {
			r = 255; g = 50; b = 50;  /* Red for chasing */
		} else if (enforcers[i].state == ENFORCER_ALERT) {
			r = 255; g = 200; b = 0;  /* Yellow for alerted */
		} else {
			r = 100; g = 100; b = 255;  /* Blue for patrolling */
		}

		/* Blinking effect for chasing enforcers */
		if (enforcers[i].state == ENFORCER_CHASE && (frameCounter / 8) % 2 == 0) {
			r = 255; g = 255; b = 255;  /* Flash white */
		}

		ptr = allocatePacket(chain, 0, 3);
		ptr[0] = gp0_rgb(r, g, b) | gp0_rectangle(false, false, false);
		ptr[1] = gp0_xy(screenX - 2, screenY - 2);
		ptr[2] = gp0_xy(5, 5);
	}
#else
	(void)enforcers;
	(void)numEnforcers;
#endif
}

/*
 * Draw detection meter when enforcers spot the player
 * Shows as a bar at bottom center of screen that fills from yellow to red
 * Includes flashing warning text
 */
void drawDetectionMeter(DMAChain *chain, int detectionLevel, int maxLevel, int frameCounter, const TextureInfo *font) {
	if (detectionLevel <= 0) return;

	/* Meter position: bottom center of screen */
	int meterWidth = 120;
	int meterHeight = 10;
	int meterX = (SCREEN_WIDTH - meterWidth) / 2;
	int meterY = SCREEN_HEIGHT - 25;

	uint32_t *ptr;

	/* Background (dark red) */
	ptr = allocatePacket(chain, 0, 3);
	ptr[0] = gp0_rgb(60, 20, 20) | gp0_rectangle(false, false, false);
	ptr[1] = gp0_xy(meterX, meterY);
	ptr[2] = gp0_xy(meterWidth, meterHeight);

	/* Fill based on detection level */
	int fillWidth = (detectionLevel * (meterWidth - 2)) / maxLevel;
	if (fillWidth > meterWidth - 2) fillWidth = meterWidth - 2;

	if (fillWidth > 0) {
		/* Color: yellow to red based on fill */
		int ratio = (detectionLevel * 255) / maxLevel;
		uint8_t r = 255;
		uint8_t g = 255 - ratio;
		uint8_t b = 0;

		ptr = allocatePacket(chain, 0, 3);
		ptr[0] = gp0_rgb(r, g, b) | gp0_rectangle(false, false, false);
		ptr[1] = gp0_xy(meterX + 1, meterY + 1);
		ptr[2] = gp0_xy(fillWidth, meterHeight - 2);
	}

	/* Flashing warning text above meter */
	if ((frameCounter / 10) % 2 == 0) {
		const char *warning;
		uint8_t textR, textG, textB;

		if (detectionLevel >= maxLevel) {
			/* Full alert - CHASE! */
			warning = "!! CAUGHT !!";
			textR = 255; textG = 50; textB = 50;
		} else if (detectionLevel > maxLevel / 2) {
			/* High alert */
			warning = "! SPOTTED !";
			textR = 255; textG = 100; textB = 0;
		} else {
			/* Low alert */
			warning = "?";
			textR = 255; textG = 200; textB = 0;
		}

		int textX = (SCREEN_WIDTH - 12 * 5) / 2;  /* Approximate center */
		int textY = meterY - 12;

		/* Shadow */
		printStringColorZ(chain, font, textX + 1, textY + 1, warning, 30, 10, 10, 1);
		/* Text */
		printStringColorZ(chain, font, textX, textY, warning, textR, textG, textB, 0);
	}
}
