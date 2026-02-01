/*
 * UI - HUD elements, pause map, and in-game overlays
 */

#pragma once

#include <stdint.h>
#include "gpu.h"

/* Screen resolution */
#define SCREEN_WIDTH  320
#define SCREEN_HEIGHT 240

#ifdef __cplusplus
extern "C" {
#endif

/* Draw the pause screen map view */
void drawPauseMap(DMAChain *chain, int32_t playerWorldX, int32_t playerWorldZ,
	int16_t playerFacing, int frameCounter);

#ifdef __cplusplus
}
#endif
