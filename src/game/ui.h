/*
 * UI - HUD elements, pause map, and in-game overlays
 */

#pragma once

#include <stdint.h>
#include "gpu.h"
#include "game_types.h"
#include "game_config.h"

/* Screen resolution */
#define SCREEN_WIDTH  320
#define SCREEN_HEIGHT 240

#ifdef __cplusplus
extern "C" {
#endif

/* Draw the pause screen map view
 * When DEBUG_VISUAL_AGENTS is enabled, also draws enforcer positions */
void drawPauseMap(DMAChain *chain, int32_t playerWorldX, int32_t playerWorldZ,
	int16_t playerFacing, int frameCounter,
	const Enforcer *enforcers, int numEnforcers);

/* Draw the detection meter when enforcers are alerted
 * Shows at bottom center with flashing warning text */
void drawDetectionMeter(DMAChain *chain, int detectionLevel, int maxLevel,
	int frameCounter, const TextureInfo *font);

#ifdef __cplusplus
}
#endif
