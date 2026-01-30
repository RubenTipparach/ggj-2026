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
#include "game_config.h"

/* Floor settings */
#define FLOOR_TILE_SIZE  64      /* Size of each floor tile in world units */
#define FLOOR_GRID_SIZE  8       /* Number of tiles in each direction from center */
#define FLOOR_Y          80      /* Y position of floor (below character) */

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

/* Draw a checkerboard floor for parallax reference */
static void drawFloor(DMAChain *chain, int32_t camX, int32_t camY, int32_t camZ) {
	/* Set up identity rotation matrix and camera translation */
	gte_setRotationMatrix(
		ONE, 0, 0,
		0, ONE, 0,
		0, 0, ONE
	);

	/* Calculate which tiles are visible based on camera position */
	int baseTileX = camX / FLOOR_TILE_SIZE;
	int baseTileZ = camZ / FLOOR_TILE_SIZE;

	/* Draw grid of floor tiles */
	for (int tz = -FLOOR_GRID_SIZE; tz < FLOOR_GRID_SIZE; tz++) {
		for (int tx = -FLOOR_GRID_SIZE; tx < FLOOR_GRID_SIZE; tx++) {
			int tileX = baseTileX + tx;
			int tileZ = baseTileZ + tz;

			/* Calculate world position of tile corners */
			int32_t x0 = tileX * FLOOR_TILE_SIZE - camX;
			int32_t x1 = x0 + FLOOR_TILE_SIZE;
			int32_t z0 = tileZ * FLOOR_TILE_SIZE - camZ + CAMERA_DISTANCE;
			int32_t z1 = z0 + FLOOR_TILE_SIZE;
			int32_t y = FLOOR_Y - camY;

			/* Skip tiles behind camera */
			if (z0 < 10 && z1 < 10) continue;
			/* Clamp near plane */
			if (z0 < 10) z0 = 10;
			if (z1 < 10) z1 = 10;

			/* Checkerboard pattern based on tile coordinates */
			int isWhite = ((tileX + tileZ) & 1);
			uint8_t r, g, b;
			if (isWhite) {
				r = 80; g = 80; b = 100;  /* Light tile */
			} else {
				r = 40; g = 40; b = 60;   /* Dark tile */
			}

			/* Set translation for GTE */
			gte_setControlReg(GTE_TRX, 0);
			gte_setControlReg(GTE_TRY, 0);
			gte_setControlReg(GTE_TRZ, 0);

			/* Transform 4 corners of tile */
			GTEVector16 v0 = {x0, y, z0, 0};
			GTEVector16 v1 = {x1, y, z0, 0};
			GTEVector16 v2 = {x1, y, z1, 0};
			GTEVector16 v3 = {x0, y, z1, 0};

			/* Triangle 1: v0, v1, v2 */
			gte_loadV0(&v0);
			gte_loadV1(&v1);
			gte_loadV2(&v2);
			gte_command(GTE_CMD_RTPT | GTE_SF);

			/* Get average Z for depth sorting */
			gte_command(GTE_CMD_AVSZ3 | GTE_SF);
			int zIndex = gte_getDataReg(GTE_OTZ);
			if (zIndex >= 0 && zIndex < ORDERING_TABLE_SIZE) {
				uint32_t *ptr = allocatePacket(chain, zIndex, 4);
				ptr[0] = gp0_rgb(r, g, b) | gp0_triangle(false, false);
				gte_storeDataReg(GTE_SXY0, 1 * 4, ptr);
				gte_storeDataReg(GTE_SXY1, 2 * 4, ptr);
				gte_storeDataReg(GTE_SXY2, 3 * 4, ptr);
			}

			/* Triangle 2: v0, v2, v3 */
			gte_loadV0(&v0);
			gte_loadV1(&v2);
			gte_loadV2(&v3);
			gte_command(GTE_CMD_RTPT | GTE_SF);

			gte_command(GTE_CMD_AVSZ3 | GTE_SF);
			zIndex = gte_getDataReg(GTE_OTZ);
			if (zIndex >= 0 && zIndex < ORDERING_TABLE_SIZE) {
				uint32_t *ptr = allocatePacket(chain, zIndex, 4);
				ptr[0] = gp0_rgb(r, g, b) | gp0_triangle(false, false);
				gte_storeDataReg(GTE_SXY0, 1 * 4, ptr);
				gte_storeDataReg(GTE_SXY1, 2 * 4, ptr);
				gte_storeDataReg(GTE_SXY2, 3 * 4, ptr);
			}
		}
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

	/* Enable DMA channels */
	DMA_DPCR |= 0
	| DMA_DPCR_CH_ENABLE(DMA_GPU)
	| DMA_DPCR_CH_ENABLE(DMA_OTC);

	GPU_GP1 = gp1_dmaRequestMode(GP1_DREQ_GP0_WRITE);
	GPU_GP1 = gp1_dispBlank(false);

	/* Upload font to VRAM */
	TextureInfo font;
	uploadIndexedTexture(
		&font,
		fontTexture,
		fontPalette,
		SCREEN_WIDTH * 2,
		0,
		SCREEN_WIDTH * 2,
		FONT_HEIGHT,
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
	initCDDA();
	puts("CD-DA initialized - music playing from disc");

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

	/* Double buffering */
	DMAChain dmaChains[2];
	bool     usingSecondFrame = false;

	/* Camera position (follows player smoothly) */
	int32_t cameraX = 0;
	int32_t cameraY = 0;
	int32_t cameraZ = 0;

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

		/* Poll controller */
		ControllerState pad;
		pollController(0, &pad);

		/* Get movement input */
		int16_t moveX = 0;
		int16_t moveZ = 0;

		/* Analog stick input */
		if (pad.isAnalog) {
			int stickX = (int)pad.leftX - 0x80;
			int stickY = (int)pad.leftY - 0x80;

			if (stickX > ANALOG_DEADZONE) moveX = 1;
			else if (stickX < -ANALOG_DEADZONE) moveX = -1;

			if (stickY > ANALOG_DEADZONE) moveZ = -1;  /* Forward is -Z */
			else if (stickY < -ANALOG_DEADZONE) moveZ = 1;
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

		/* Update character animation */
		updateCharacter(&player, moveX, moveZ);

		/* Update CD-DA looping */
		updateCDDA();

		/* Camera follows player smoothly */
		int32_t targetCamX = player.x >> 12;
		int32_t targetCamZ = player.z >> 12;

		/* Smooth camera follow (lerp) */
		cameraX += (targetCamX - cameraX) / CAMERA_FOLLOW_DIVISOR;
		cameraZ += (targetCamZ - cameraZ) / CAMERA_FOLLOW_DIVISOR;

		/* Draw floor for parallax reference */
		drawFloor(chain, cameraX, cameraY, cameraZ);

		/* Draw character */
		drawCharacter(chain, &player, cameraX, cameraY, cameraZ);

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
		waitForGP0Ready();
		waitForVSync();
		sendLinkedList(&(chain->orderingTable)[ORDERING_TABLE_SIZE - 1]);
	}

	return 0;
}
