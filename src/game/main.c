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
#include "adult_male_L_offsets.h"
#include "citizen_1_offsets.h"
#include "citizen_2_offsets.h"

/* New modular headers */
#include "game_types.h"
#include "collision.h"
#include "rendering.h"
#include "input.h"
#include "ui.h"

/* Performance stats */
static int32_t statFrameTime = 0;    /* Frame time in timer ticks */
static int32_t statGpuWait = 0;      /* GPU wait time in timer ticks */
static int32_t statFloorTime = 0;    /* Floor drawing time */
static int32_t statCharTime = 0;     /* Character drawing time */
static int32_t statPadTime = 0;      /* Controller polling time */

/* Frame timing for delta time calculation */
/* Timer runs at CPU/8 = ~4.2336 MHz. At 60fps, one frame = ~70560 ticks */
/* deltaTime 256 = 1.0 (one 60fps frame). Higher = slower fps, game catches up */
#define TICKS_PER_FRAME_60FPS 70560
#define DELTA_TIME_MAX 512  /* Cap at 2x (30fps minimum before slowdown) */

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

/* Mask model data embedded by CMake */
extern const uint8_t maskData[];
extern const uint32_t maskData_size;

/* Adult male (dad) character data embedded by CMake */
extern const uint8_t dadBodyData[];
extern const uint32_t dadBodyData_size;
extern const uint8_t dadHeadData[];
extern const uint32_t dadHeadData_size;
extern const uint8_t dadArmLeftData[];
extern const uint32_t dadArmLeftData_size;
extern const uint8_t dadArmRightData[];
extern const uint32_t dadArmRightData_size;
extern const uint8_t dadLegLeftData[];
extern const uint32_t dadLegLeftData_size;
extern const uint8_t dadLegRightData[];
extern const uint32_t dadLegRightData_size;

/* Citizen 1 character data embedded by CMake */
extern const uint8_t citizen1BodyData[];
extern const uint32_t citizen1BodyData_size;
extern const uint8_t citizen1HeadData[];
extern const uint32_t citizen1HeadData_size;
extern const uint8_t citizen1ArmLeftData[];
extern const uint32_t citizen1ArmLeftData_size;
extern const uint8_t citizen1ArmRightData[];
extern const uint32_t citizen1ArmRightData_size;
extern const uint8_t citizen1LegLeftData[];
extern const uint32_t citizen1LegLeftData_size;
extern const uint8_t citizen1LegRightData[];
extern const uint32_t citizen1LegRightData_size;

/* Citizen 2 character data embedded by CMake */
extern const uint8_t citizen2BodyData[];
extern const uint32_t citizen2BodyData_size;
extern const uint8_t citizen2HeadData[];
extern const uint32_t citizen2HeadData_size;
extern const uint8_t citizen2ArmLeftData[];
extern const uint32_t citizen2ArmLeftData_size;
extern const uint8_t citizen2ArmRightData[];
extern const uint32_t citizen2ArmRightData_size;
extern const uint8_t citizen2LegLeftData[];
extern const uint32_t citizen2LegLeftData_size;
extern const uint8_t citizen2LegRightData[];
extern const uint32_t citizen2LegRightData_size;

/* Enforcer model parts embedded by CMake */
extern const uint8_t enforcerBodyData[];
extern const uint32_t enforcerBodyData_size;
extern const uint8_t enforcerLegLeftData[];
extern const uint32_t enforcerLegLeftData_size;
extern const uint8_t enforcerLegRightData[];
extern const uint32_t enforcerLegRightData_size;

/* Number of houses - use map data */
#define NUM_HOUSES NUM_MAP_HOUSES

/* Font data embedded by CMake */
extern const uint8_t fontTexture[];
extern const uint8_t fontPalette[];

/* Grass texture data embedded by CMake (32x32 16bpp) */
extern const uint8_t grassTexture[];

/* Title screen texture data embedded by CMake (256x256 16bpp) */
extern const uint8_t titleTexture[];

/* Sound effects data embedded by CMake (SPU-ADPCM format) */
/* Music uses CD-DA from disc tracks */
extern const uint8_t sfxBiteData[];
extern const uint32_t sfxBiteData_size;
extern const uint8_t sfxStepGrassData[];
extern const uint32_t sfxStepGrassData_size;
extern const uint8_t sfxStepWoodData[];
extern const uint32_t sfxStepWoodData_size;

/* Font dimensions */
#define FONT_WIDTH        96
#define FONT_HEIGHT       56
#define FONT_COLOR_DEPTH  GP0_COLOR_4BPP

/* Grass texture dimensions (32x32 16bpp) */
#define GRASS_TEX_WIDTH   32
#define GRASS_TEX_HEIGHT  32

/* Title screen texture dimensions (256x256 16bpp) */
#define TITLE_TEX_WIDTH   256
#define TITLE_TEX_HEIGHT  256

/* GTE uses 20.12 fixed-point format */
#define ONE (1 << 12)

/* Screen center position */
#define CENTERX (SCREEN_WIDTH  / 2)
#define CENTERY (SCREEN_HEIGHT / 2)

/* Initialize the GTE for 3D rendering */
static void setupGTE(int width, int height)
{
	/* Enable coprocessor 2 (GTE) */
	cop0_setReg(COP0_STATUS, cop0_getReg(COP0_STATUS) | COP0_STATUS_CU2);

	/* Set screen offset (center of screen) - 16.16 fixed-point */
	gte_setControlReg(GTE_OFX, (width  << 16) / 2);
	gte_setControlReg(GTE_OFY, (height << 16) / 2);

	/* Set projection plane distance (FOV control) */
	gte_setControlReg(GTE_H, CAMERA_FOCAL_LENGTH);

	/* Set Z averaging scale factors for ordering table sorting */
	gte_setControlReg(GTE_ZSF3, ORDERING_TABLE_SIZE / 3);
	gte_setControlReg(GTE_ZSF4, ORDERING_TABLE_SIZE / 4);
}

/* Initialize Timer 2 for frame timing */
static void setupTimer(void)
{
	TIMER_CTRL(2) = 0;                        /* Stop timer */
	TIMER_VALUE(2) = 0;                       /* Reset counter */
	TIMER_CTRL(2) = TIMER_CTRL_PRESCALE;      /* CPU/8, free running */
}

int main(int argc, const char **argv)
{
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

	/* Upload font to VRAM */
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
	TextureInfo grassTex;
	uploadTexture(
		&grassTex,
		grassTexture,
		SCREEN_WIDTH * 2,          /* Image X = 640 */
		320,                       /* Image Y = 320 (below font) */
		GRASS_TEX_WIDTH,
		GRASS_TEX_HEIGHT
	);
	renderingSetGrassTexture(&grassTex);
	puts("Grass texture uploaded to VRAM");

	/* Upload title screen texture to VRAM (256x256 16bpp) */
	TextureInfo titleTex;
	uploadTexture(
		&titleTex,
		titleTexture,
		SCREEN_WIDTH * 2,          /* Image X = 640 */
		0,                         /* Image Y = 0 (top of texture page) */
		TITLE_TEX_WIDTH,
		TITLE_TEX_HEIGHT
	);
	puts("Title texture uploaded to VRAM");

	/* Initialize SPU */
	setupSPU();
	puts("SPU initialized");

	/* Initialize BIOS events for HLE compatibility */
	biosInit();
	puts("BIOS events initialized");

	/* Upload SPU sound effects */
	uint32_t sfxBiteAddr = 0;
	uint32_t sfxStepGrassAddr = 0;
	uint32_t sfxStepWoodAddr = 0;
	if (sfxBiteData_size > 0) {
		sfxBiteAddr = uploadVAG(sfxBiteData, sfxBiteData_size);
		printf("SPU: Bite SFX uploaded to 0x%05lX\n", (unsigned long)sfxBiteAddr);
	}
	if (sfxStepGrassData_size > 0) {
		sfxStepGrassAddr = uploadVAG(sfxStepGrassData, sfxStepGrassData_size);
		printf("SPU: Grass step SFX uploaded to 0x%05lX\n", (unsigned long)sfxStepGrassAddr);
	}
	if (sfxStepWoodData_size > 0) {
		sfxStepWoodAddr = uploadVAG(sfxStepWoodData, sfxStepWoodData_size);
		printf("SPU: Wood step SFX uploaded to 0x%05lX\n", (unsigned long)sfxStepWoodAddr);
	}

	/* Initialize CD-DA for music playback */
	/* NOTE: Do this BEFORE spuUnmute() since initCDDA() touches SPU_CTRL */
	initCDDA();
	puts("CD-DA initialized");

	/* Unmute SPU AFTER CD-DA init */
	spuUnmute();
	puts("SPU initialized");

	/* Music state - uses CD-DA tracks */
	#define CDDA_TRACK_INTRO 2     /* Track 2: Intro music */
	#define CDDA_TRACK_GAMEPLAY 3  /* Track 3: Gameplay loop */
	#define MUSIC_NONE 0
	#define MUSIC_INTRO 1
	#define MUSIC_GAMEPLAY 2
	int currentMusic = MUSIC_NONE;  /* Music starts when player presses Start */
	bool showingCredits = false;
	int32_t creditsTimer = 0;
	#define CREDITS_DURATION (60 * 5)  /* 5 seconds total at 60fps */
	#define CREDIT_HALF_DURATION (CREDITS_DURATION / 2)  /* 2.5 seconds per credit */
	bool firstTimeLeaving = true;

	/* SFX channels and settings */
	#define SFX_CHANNEL_STEP 0
	#define FOOTSTEP_VOLUME 0x2800  /* Quieter than other SFX */
	int16_t lastWalkCycle = 0;  /* Track walk cycle for footstep timing */

	/* Initialize character */
	Character player;
	if (!initCharacter(&player,
		charBodyData, charBodyData_size,
		charHeadData, charHeadData_size,
		charArmLeftData, charArmLeftData_size,
		charArmRightData, charArmRightData_size,
		charLegLeftData, charLegLeftData_size,
		charLegRightData, charLegRightData_size)) {
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
		momLegRightData, momLegRightData_size)) {
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

	/* Initialize hiding adult characters (use dad model for now) */
	Character hidingAdultChars[NUM_HIDING_ADULTS];
	for (int i = 0; i < NUM_HIDING_ADULTS; i++) {
		if (!initCharacter(&hidingAdultChars[i],
			dadBodyData, dadBodyData_size,
			dadHeadData, dadHeadData_size,
			dadArmLeftData, dadArmLeftData_size,
			dadArmRightData, dadArmRightData_size,
			dadLegLeftData, dadLegLeftData_size,
			dadLegRightData, dadLegRightData_size)) {
			printf("Failed to init hiding adult %d!\n", i);
			return 1;
		}
		/* Set body part offsets from adult_male_L_offsets.h */
		hidingAdultChars[i].partOffsetX[PART_BODY] = ADULT_MALE_L_BODY_OFFSET_X;
		hidingAdultChars[i].partOffsetY[PART_BODY] = ADULT_MALE_L_BODY_OFFSET_Y;
		hidingAdultChars[i].partOffsetZ[PART_BODY] = ADULT_MALE_L_BODY_OFFSET_Z;
		hidingAdultChars[i].partOffsetX[PART_HEAD] = ADULT_MALE_L_HEAD_OFFSET_X;
		hidingAdultChars[i].partOffsetY[PART_HEAD] = ADULT_MALE_L_HEAD_OFFSET_Y;
		hidingAdultChars[i].partOffsetZ[PART_HEAD] = ADULT_MALE_L_HEAD_OFFSET_Z;
		hidingAdultChars[i].partOffsetX[PART_ARM_LEFT] = ADULT_MALE_L_ARM_LEFT_OFFSET_X;
		hidingAdultChars[i].partOffsetY[PART_ARM_LEFT] = ADULT_MALE_L_ARM_LEFT_OFFSET_Y;
		hidingAdultChars[i].partOffsetZ[PART_ARM_LEFT] = ADULT_MALE_L_ARM_LEFT_OFFSET_Z;
		hidingAdultChars[i].partOffsetX[PART_ARM_RIGHT] = ADULT_MALE_L_ARM_RIGHT_OFFSET_X;
		hidingAdultChars[i].partOffsetY[PART_ARM_RIGHT] = ADULT_MALE_L_ARM_RIGHT_OFFSET_Y;
		hidingAdultChars[i].partOffsetZ[PART_ARM_RIGHT] = ADULT_MALE_L_ARM_RIGHT_OFFSET_Z;
		hidingAdultChars[i].partOffsetX[PART_LEG_LEFT] = ADULT_MALE_L_LEG_LEFT_OFFSET_X;
		hidingAdultChars[i].partOffsetY[PART_LEG_LEFT] = ADULT_MALE_L_LEG_LEFT_OFFSET_Y;
		hidingAdultChars[i].partOffsetZ[PART_LEG_LEFT] = ADULT_MALE_L_LEG_LEFT_OFFSET_Z;
		hidingAdultChars[i].partOffsetX[PART_LEG_RIGHT] = ADULT_MALE_L_LEG_RIGHT_OFFSET_X;
		hidingAdultChars[i].partOffsetY[PART_LEG_RIGHT] = ADULT_MALE_L_LEG_RIGHT_OFFSET_Y;
		hidingAdultChars[i].partOffsetZ[PART_LEG_RIGHT] = ADULT_MALE_L_LEG_RIGHT_OFFSET_Z;
		hidingAdultChars[i].facing = 0; /* Face away from camera initially */
	}
	puts("Hiding adult characters initialized!");

#if DEBUG_CHARACTERS
	/* Initialize restaurant citizen characters (only in debug mode for testing) */
	Character restaurantCitizens[NUM_RESTAURANT_CITIZENS];
	/* Citizen 0 uses citizen1 model */
	if (!initCharacter(&restaurantCitizens[0],
		citizen1BodyData, citizen1BodyData_size,
		citizen1HeadData, citizen1HeadData_size,
		citizen1ArmLeftData, citizen1ArmLeftData_size,
		citizen1ArmRightData, citizen1ArmRightData_size,
		citizen1LegLeftData, citizen1LegLeftData_size,
		citizen1LegRightData, citizen1LegRightData_size)) {
		puts("Failed to init citizen 0!");
		return 1;
	}
	restaurantCitizens[0].partOffsetX[PART_BODY] = CITIZEN_1_BODY_OFFSET_X;
	restaurantCitizens[0].partOffsetY[PART_BODY] = CITIZEN_1_BODY_OFFSET_Y;
	restaurantCitizens[0].partOffsetZ[PART_BODY] = CITIZEN_1_BODY_OFFSET_Z;
	restaurantCitizens[0].partOffsetX[PART_HEAD] = CITIZEN_1_HEAD_OFFSET_X;
	restaurantCitizens[0].partOffsetY[PART_HEAD] = CITIZEN_1_HEAD_OFFSET_Y;
	restaurantCitizens[0].partOffsetZ[PART_HEAD] = CITIZEN_1_HEAD_OFFSET_Z;
	restaurantCitizens[0].partOffsetX[PART_ARM_LEFT] = CITIZEN_1_ARM_LEFT_OFFSET_X;
	restaurantCitizens[0].partOffsetY[PART_ARM_LEFT] = CITIZEN_1_ARM_LEFT_OFFSET_Y;
	restaurantCitizens[0].partOffsetZ[PART_ARM_LEFT] = CITIZEN_1_ARM_LEFT_OFFSET_Z;
	restaurantCitizens[0].partOffsetX[PART_ARM_RIGHT] = CITIZEN_1_ARM_RIGHT_OFFSET_X;
	restaurantCitizens[0].partOffsetY[PART_ARM_RIGHT] = CITIZEN_1_ARM_RIGHT_OFFSET_Y;
	restaurantCitizens[0].partOffsetZ[PART_ARM_RIGHT] = CITIZEN_1_ARM_RIGHT_OFFSET_Z;
	restaurantCitizens[0].partOffsetX[PART_LEG_LEFT] = CITIZEN_1_LEG_LEFT_OFFSET_X;
	restaurantCitizens[0].partOffsetY[PART_LEG_LEFT] = CITIZEN_1_LEG_LEFT_OFFSET_Y;
	restaurantCitizens[0].partOffsetZ[PART_LEG_LEFT] = CITIZEN_1_LEG_LEFT_OFFSET_Z;
	restaurantCitizens[0].partOffsetX[PART_LEG_RIGHT] = CITIZEN_1_LEG_RIGHT_OFFSET_X;
	restaurantCitizens[0].partOffsetY[PART_LEG_RIGHT] = CITIZEN_1_LEG_RIGHT_OFFSET_Y;
	restaurantCitizens[0].partOffsetZ[PART_LEG_RIGHT] = CITIZEN_1_LEG_RIGHT_OFFSET_Z;
	restaurantCitizens[0].x = CITIZEN_0_POS_X << 12;
	restaurantCitizens[0].y = CITIZEN_0_POS_Y << 12;
	restaurantCitizens[0].z = CITIZEN_0_POS_Z << 12;
	restaurantCitizens[0].facing = 0; /* Face away from camera initially */

	/* Citizen 1 uses citizen2 model */
	if (!initCharacter(&restaurantCitizens[1],
		citizen2BodyData, citizen2BodyData_size,
		citizen2HeadData, citizen2HeadData_size,
		citizen2ArmLeftData, citizen2ArmLeftData_size,
		citizen2ArmRightData, citizen2ArmRightData_size,
		citizen2LegLeftData, citizen2LegLeftData_size,
		citizen2LegRightData, citizen2LegRightData_size)) {
		puts("Failed to init citizen 1!");
		return 1;
	}
	restaurantCitizens[1].partOffsetX[PART_BODY] = CITIZEN_2_BODY_OFFSET_X;
	restaurantCitizens[1].partOffsetY[PART_BODY] = CITIZEN_2_BODY_OFFSET_Y;
	restaurantCitizens[1].partOffsetZ[PART_BODY] = CITIZEN_2_BODY_OFFSET_Z;
	restaurantCitizens[1].partOffsetX[PART_HEAD] = CITIZEN_2_HEAD_OFFSET_X;
	restaurantCitizens[1].partOffsetY[PART_HEAD] = CITIZEN_2_HEAD_OFFSET_Y;
	restaurantCitizens[1].partOffsetZ[PART_HEAD] = CITIZEN_2_HEAD_OFFSET_Z;
	restaurantCitizens[1].partOffsetX[PART_ARM_LEFT] = CITIZEN_2_ARM_LEFT_OFFSET_X;
	restaurantCitizens[1].partOffsetY[PART_ARM_LEFT] = CITIZEN_2_ARM_LEFT_OFFSET_Y;
	restaurantCitizens[1].partOffsetZ[PART_ARM_LEFT] = CITIZEN_2_ARM_LEFT_OFFSET_Z;
	restaurantCitizens[1].partOffsetX[PART_ARM_RIGHT] = CITIZEN_2_ARM_RIGHT_OFFSET_X;
	restaurantCitizens[1].partOffsetY[PART_ARM_RIGHT] = CITIZEN_2_ARM_RIGHT_OFFSET_Y;
	restaurantCitizens[1].partOffsetZ[PART_ARM_RIGHT] = CITIZEN_2_ARM_RIGHT_OFFSET_Z;
	restaurantCitizens[1].partOffsetX[PART_LEG_LEFT] = CITIZEN_2_LEG_LEFT_OFFSET_X;
	restaurantCitizens[1].partOffsetY[PART_LEG_LEFT] = CITIZEN_2_LEG_LEFT_OFFSET_Y;
	restaurantCitizens[1].partOffsetZ[PART_LEG_LEFT] = CITIZEN_2_LEG_LEFT_OFFSET_Z;
	restaurantCitizens[1].partOffsetX[PART_LEG_RIGHT] = CITIZEN_2_LEG_RIGHT_OFFSET_X;
	restaurantCitizens[1].partOffsetY[PART_LEG_RIGHT] = CITIZEN_2_LEG_RIGHT_OFFSET_Y;
	restaurantCitizens[1].partOffsetZ[PART_LEG_RIGHT] = CITIZEN_2_LEG_RIGHT_OFFSET_Z;
	restaurantCitizens[1].x = CITIZEN_1_POS_X << 12;
	restaurantCitizens[1].y = CITIZEN_1_POS_Y << 12;
	restaurantCitizens[1].z = CITIZEN_1_POS_Z << 12;
	restaurantCitizens[1].facing = 0; /* Face away from camera initially */
	puts("Restaurant citizens initialized!");
#endif

	/* Initialize house citizen characters (2 per house) */
	Character houseCitizens[NUM_HOUSES][NUM_CITIZENS_PER_HOUSE];
	for (int h = 0; h < NUM_HOUSES; h++) {
		/* Citizen 0 in each house uses citizen1 model */
		if (!initCharacter(&houseCitizens[h][0],
			citizen1BodyData, citizen1BodyData_size,
			citizen1HeadData, citizen1HeadData_size,
			citizen1ArmLeftData, citizen1ArmLeftData_size,
			citizen1ArmRightData, citizen1ArmRightData_size,
			citizen1LegLeftData, citizen1LegLeftData_size,
			citizen1LegRightData, citizen1LegRightData_size)) {
			printf("Failed to init house %d citizen 0!\n", h);
			return 1;
		}
		houseCitizens[h][0].partOffsetX[PART_BODY] = CITIZEN_1_BODY_OFFSET_X;
		houseCitizens[h][0].partOffsetY[PART_BODY] = CITIZEN_1_BODY_OFFSET_Y;
		houseCitizens[h][0].partOffsetZ[PART_BODY] = CITIZEN_1_BODY_OFFSET_Z;
		houseCitizens[h][0].partOffsetX[PART_HEAD] = CITIZEN_1_HEAD_OFFSET_X;
		houseCitizens[h][0].partOffsetY[PART_HEAD] = CITIZEN_1_HEAD_OFFSET_Y;
		houseCitizens[h][0].partOffsetZ[PART_HEAD] = CITIZEN_1_HEAD_OFFSET_Z;
		houseCitizens[h][0].partOffsetX[PART_ARM_LEFT] = CITIZEN_1_ARM_LEFT_OFFSET_X;
		houseCitizens[h][0].partOffsetY[PART_ARM_LEFT] = CITIZEN_1_ARM_LEFT_OFFSET_Y;
		houseCitizens[h][0].partOffsetZ[PART_ARM_LEFT] = CITIZEN_1_ARM_LEFT_OFFSET_Z;
		houseCitizens[h][0].partOffsetX[PART_ARM_RIGHT] = CITIZEN_1_ARM_RIGHT_OFFSET_X;
		houseCitizens[h][0].partOffsetY[PART_ARM_RIGHT] = CITIZEN_1_ARM_RIGHT_OFFSET_Y;
		houseCitizens[h][0].partOffsetZ[PART_ARM_RIGHT] = CITIZEN_1_ARM_RIGHT_OFFSET_Z;
		houseCitizens[h][0].partOffsetX[PART_LEG_LEFT] = CITIZEN_1_LEG_LEFT_OFFSET_X;
		houseCitizens[h][0].partOffsetY[PART_LEG_LEFT] = CITIZEN_1_LEG_LEFT_OFFSET_Y;
		houseCitizens[h][0].partOffsetZ[PART_LEG_LEFT] = CITIZEN_1_LEG_LEFT_OFFSET_Z;
		houseCitizens[h][0].partOffsetX[PART_LEG_RIGHT] = CITIZEN_1_LEG_RIGHT_OFFSET_X;
		houseCitizens[h][0].partOffsetY[PART_LEG_RIGHT] = CITIZEN_1_LEG_RIGHT_OFFSET_Y;
		houseCitizens[h][0].partOffsetZ[PART_LEG_RIGHT] = CITIZEN_1_LEG_RIGHT_OFFSET_Z;
		houseCitizens[h][0].x = HOUSE_CITIZEN_0_POS_X << 12;
		houseCitizens[h][0].y = HOUSE_CITIZEN_0_POS_Y << 12;
		houseCitizens[h][0].z = HOUSE_CITIZEN_0_POS_Z << 12;
		houseCitizens[h][0].facing = 0;

		/* Citizen 1 in each house uses citizen2 model */
		if (!initCharacter(&houseCitizens[h][1],
			citizen2BodyData, citizen2BodyData_size,
			citizen2HeadData, citizen2HeadData_size,
			citizen2ArmLeftData, citizen2ArmLeftData_size,
			citizen2ArmRightData, citizen2ArmRightData_size,
			citizen2LegLeftData, citizen2LegLeftData_size,
			citizen2LegRightData, citizen2LegRightData_size)) {
			printf("Failed to init house %d citizen 1!\n", h);
			return 1;
		}
		houseCitizens[h][1].partOffsetX[PART_BODY] = CITIZEN_2_BODY_OFFSET_X;
		houseCitizens[h][1].partOffsetY[PART_BODY] = CITIZEN_2_BODY_OFFSET_Y;
		houseCitizens[h][1].partOffsetZ[PART_BODY] = CITIZEN_2_BODY_OFFSET_Z;
		houseCitizens[h][1].partOffsetX[PART_HEAD] = CITIZEN_2_HEAD_OFFSET_X;
		houseCitizens[h][1].partOffsetY[PART_HEAD] = CITIZEN_2_HEAD_OFFSET_Y;
		houseCitizens[h][1].partOffsetZ[PART_HEAD] = CITIZEN_2_HEAD_OFFSET_Z;
		houseCitizens[h][1].partOffsetX[PART_ARM_LEFT] = CITIZEN_2_ARM_LEFT_OFFSET_X;
		houseCitizens[h][1].partOffsetY[PART_ARM_LEFT] = CITIZEN_2_ARM_LEFT_OFFSET_Y;
		houseCitizens[h][1].partOffsetZ[PART_ARM_LEFT] = CITIZEN_2_ARM_LEFT_OFFSET_Z;
		houseCitizens[h][1].partOffsetX[PART_ARM_RIGHT] = CITIZEN_2_ARM_RIGHT_OFFSET_X;
		houseCitizens[h][1].partOffsetY[PART_ARM_RIGHT] = CITIZEN_2_ARM_RIGHT_OFFSET_Y;
		houseCitizens[h][1].partOffsetZ[PART_ARM_RIGHT] = CITIZEN_2_ARM_RIGHT_OFFSET_Z;
		houseCitizens[h][1].partOffsetX[PART_LEG_LEFT] = CITIZEN_2_LEG_LEFT_OFFSET_X;
		houseCitizens[h][1].partOffsetY[PART_LEG_LEFT] = CITIZEN_2_LEG_LEFT_OFFSET_Y;
		houseCitizens[h][1].partOffsetZ[PART_LEG_LEFT] = CITIZEN_2_LEG_LEFT_OFFSET_Z;
		houseCitizens[h][1].partOffsetX[PART_LEG_RIGHT] = CITIZEN_2_LEG_RIGHT_OFFSET_X;
		houseCitizens[h][1].partOffsetY[PART_LEG_RIGHT] = CITIZEN_2_LEG_RIGHT_OFFSET_Y;
		houseCitizens[h][1].partOffsetZ[PART_LEG_RIGHT] = CITIZEN_2_LEG_RIGHT_OFFSET_Z;
		houseCitizens[h][1].x = HOUSE_CITIZEN_1_POS_X << 12;
		houseCitizens[h][1].y = HOUSE_CITIZEN_1_POS_Y << 12;
		houseCitizens[h][1].z = HOUSE_CITIZEN_1_POS_Z << 12;
		houseCitizens[h][1].facing = 0;
	}
	printf("House citizens initialized for %d houses!\n", NUM_HOUSES);

	/* Load food box model */
	Model foodBoxModel;
	if (!loadCharacterModel(&foodBoxModel, foodBoxData, foodBoxData_size)) {
		puts("Failed to load food box model!");
		return 1;
	}
	puts("Food box model loaded!");

	/* Load mask model */
	Model maskModel;
	if (!loadCharacterModel(&maskModel, maskData, maskData_size)) {
		puts("Failed to load mask model!");
		return 1;
	}
	puts("Mask model loaded!");

	/* Load enforcer model parts (body+head, left leg, right leg) */
	Model enforcerBodyModel, enforcerLegLeftModel, enforcerLegRightModel;
	if (!loadCharacterModel(&enforcerBodyModel, enforcerBodyData, enforcerBodyData_size)) {
		puts("Failed to load enforcer body!");
		return 1;
	}
	if (!loadCharacterModel(&enforcerLegLeftModel, enforcerLegLeftData, enforcerLegLeftData_size)) {
		puts("Failed to load enforcer left leg!");
		return 1;
	}
	if (!loadCharacterModel(&enforcerLegRightModel, enforcerLegRightData, enforcerLegRightData_size)) {
		puts("Failed to load enforcer right leg!");
		return 1;
	}
	puts("Enforcer model parts loaded!");

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

	/* Set player spawn position from map data with offset */
	player.x = (PLAYER_SPAWN_X + PLAYER_SPAWN_OFFSET_X) << 12;
	player.z = (PLAYER_SPAWN_Z + PLAYER_SPAWN_OFFSET_Z) << 12;
	player.facing = PLAYER_START_ROTATION;
	player.targetFacing = PLAYER_START_ROTATION;
	printf("Player spawn: %d, %d (with offset)\n", PLAYER_SPAWN_X + PLAYER_SPAWN_OFFSET_X, PLAYER_SPAWN_Z + PLAYER_SPAWN_OFFSET_Z);

	/* Double buffering */
	DMAChain dmaChains[2];
	bool usingSecondFrame = false;

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
	GameState prePauseState = STATE_EXTERIOR;
	int currentHouseIndex = -1;
	int fadeAlpha = 0;
	int fadeHoldCounter = 0;
	int32_t entryPosX = 0;
	int32_t entryPosZ = 0;
	int16_t entryFacing = 0;
	bool transitionToInterior = false;
	bool caughtTransition = false;  /* True when transitioning due to enforcer catch */
	int frameCounter = 0;

	/* Intro text state */
	int introCharCount = 0;
	bool introTextComplete = false;

	/* Day intro state */
	int dayIntroTimer = 0;
	int currentDay = 1;

	/* Mom/delivery state */
	int momInstructionIndex = 0;      /* Which instruction line we're on */
	bool instructionsDone = false;    /* All instructions heard, food spawns */
	int momCommentaryIndex = 0;       /* Which commentary line (cycles) */
	bool talkedToMomAboutMasks = false;
	bool hasFood = false;
	bool hasMask = false;
	int targetHouseIndex = 0; /* Will be synced with correctFoodHouse */
	bool foodBoxSpawned = false;
	int32_t foodBoxX = 100 << 12;
	int32_t foodBoxZ = -100 << 12;
	int masksCollected = 0;

	/* Citizen NPC state - each house has citizens who accept/reject food */
	#define MAX_CITIZENS_PER_HOUSE 3
	/* NUM_HIDING_ADULTS defined in game_config.h */
	typedef struct {
		int houseIndex;      /* Which house this adult is hiding in (-1 = none) */
		bool hasMask;        /* Whether they've received a mask */
		bool isMale;         /* true = male, false = female */
	} HidingAdult;
	HidingAdult hidingAdults[NUM_HIDING_ADULTS];

	/* Track which house the food is meant for each day */
	int correctFoodHouse = currentDay % NUM_HOUSES;

	/* Initialize hiding adults for the day */
	for (int i = 0; i < NUM_HIDING_ADULTS; i++) {
#if DEBUG_CHARACTERS
		/* Debug: spawn adult in the restaurant */
		hidingAdults[i].houseIndex = NUM_HOUSES;  /* NUM_HOUSES = restaurant */
		hidingAdultChars[i].x = HIDING_ADULT_POS_X << 12;
		hidingAdultChars[i].y = HIDING_ADULT_POS_Y << 12;
		hidingAdultChars[i].z = HIDING_ADULT_POS_Z << 12;
#else
		/* Randomly assign to houses - MUST avoid the food delivery house! */
		int candidateHouse = (i * 3 + currentDay) % NUM_HOUSES;
		if (candidateHouse == correctFoodHouse) {
			candidateHouse = (candidateHouse + 1) % NUM_HOUSES;
		}
		hidingAdults[i].houseIndex = candidateHouse;
		/* Position in house interior */
		hidingAdultChars[i].x = HOUSE_ADULT_POS_X << 12;
		hidingAdultChars[i].y = HOUSE_ADULT_POS_Y << 12;
		hidingAdultChars[i].z = HOUSE_ADULT_POS_Z << 12;
#endif
		hidingAdults[i].hasMask = false;
		hidingAdults[i].isMale = true;  /* Male adult */
	}

	/* Track if mask was delivered this day (for day end trigger) */
	bool maskDeliveredThisDay = false;

	/*========================================================================
	 * ENFORCER SYSTEM
	 *========================================================================*/

	/* Fixed patrol positions on streets
	 * Player spawns at (-9728, 0), so horizontal corridor is at Z≈0
	 * Vertical street at X≈7168, going up from the corridor */
	static const int32_t enforcerPatrolX[MAX_ENFORCERS] = {
		0,      /* Enforcer 0: center of horizontal corridor */
		4096,   /* Enforcer 1: right of center on corridor */
		-4096,  /* Enforcer 2: left of center on corridor */
		7168,   /* Enforcer 3: vertical street */
		7168    /* Enforcer 4: upper vertical street */
	};
	static const int32_t enforcerPatrolZ[MAX_ENFORCERS] = {
		0,      /* Enforcer 0: on corridor (same Z as player spawn) */
		0,      /* Enforcer 1: on corridor */
		0,      /* Enforcer 2: on corridor */
		5120,   /* Enforcer 3: mid vertical street */
		9216    /* Enforcer 4: upper vertical street */
	};

	/* Initialize enforcers at fixed street positions
	 * Day N has N enforcers (Day 1 = 1, Day 2 = 2, etc.) */
	Enforcer enforcers[MAX_ENFORCERS];
	for (int i = 0; i < MAX_ENFORCERS; i++) {
		enforcers[i].isActive = (i < currentDay);  /* Day N has N enforcers */
		enforcers[i].state = ENFORCER_PATROL;
		enforcers[i].detectionMeter = 0;
		enforcers[i].cooldownTimer = 0;

		/* Use fixed patrol positions on streets */
		enforcers[i].patrolCenterX = enforcerPatrolX[i];
		enforcers[i].patrolCenterZ = enforcerPatrolZ[i];
		enforcers[i].x = enforcers[i].patrolCenterX << 12;
		enforcers[i].z = enforcers[i].patrolCenterZ << 12;
		enforcers[i].y = ENFORCER_SPAWN_Y << 12;
		enforcers[i].facing = (i * 1024) & 0xFFF;  /* Stagger initial facing */
		enforcers[i].patrolWaypoint = 0;
		enforcers[i].waypointTimer = WAYPOINT_PAUSE_TIME;
		enforcers[i].walkCycle = 0;
	}

#if DEBUG_ENFORCER_NEARBY
	/* Debug: Spawn enforcer 0 near the restaurant for testing */
	enforcers[0].isActive = true;
	enforcers[0].patrolCenterX = PLAYER_SPAWN_X + 800;  /* Just east of restaurant */
	enforcers[0].patrolCenterZ = PLAYER_SPAWN_Z + 800;  /* Just north of restaurant */
	enforcers[0].x = enforcers[0].patrolCenterX << 12;
	enforcers[0].z = enforcers[0].patrolCenterZ << 12;
	printf("DEBUG: Enforcer spawned near restaurant at (%d, %d)\n",
		enforcers[0].patrolCenterX, enforcers[0].patrolCenterZ);
#endif
	int maxDetectionLevel = 0;  /* Track highest detection for UI */
	bool playerCaught = false;  /* Set when enforcer catches player */

	/* Dialog state */
	const char *currentDialog = NULL;
	int dialogCharCount = 0;
	bool dialogComplete = false;
	int dialogAccum = 0;  /* Accumulator for typewriter effect (adds deltaTime, advances char at 512) */

	/* Delta time - calculated based on actual frame time */
	/* 256 = 1.0 (one 60fps frame). Will scale with actual frame rate */

	puts("Character demo starting...");
	puts("Use D-pad or left stick to walk");
	puts("Press X for sound effect");

	/* Main loop */
	for (;;) {
		int bufferX = usingSecondFrame ? SCREEN_WIDTH : 0;
		int bufferY = 0;

		DMAChain *chain = &dmaChains[usingSecondFrame];
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

		/* Calculate delta time based on previous frame's CPU time */
		/* Only scale UP when frames take longer than expected (dropping frames) */
		/* Use 256 (1.0) as baseline - don't scale down for fast frames since vsync pads them */
		int deltaTime;
		if (statFrameTime > TICKS_PER_FRAME_60FPS) {
			/* Frame took longer than 1/60th second - scale up to compensate */
			deltaTime = (statFrameTime * 256) / TICKS_PER_FRAME_60FPS;
			if (deltaTime > DELTA_TIME_MAX) deltaTime = DELTA_TIME_MAX;  /* Max 2x (30fps floor) */
		} else {
			deltaTime = 256;  /* Normal frame - use standard 1.0 delta */
		}

		/* Poll controller */
		uint16_t t0 = TIMER_VALUE(2);
		ControllerState pad;
		pollController(0, &pad);
		uint16_t t1 = TIMER_VALUE(2);
		statPadTime = (uint16_t)(t1 - t0);

		/* Get movement input (disabled during fade transitions) */
		int16_t moveX = 0;
		int16_t moveZ = 0;
		bool strafeMode = false;
		int32_t strafeDirX = 0;
		int32_t strafeDirZ = 0;

		/* Only process input when not fading */
		bool canProcessInput = (gameState == STATE_EXTERIOR || gameState == STATE_INTERIOR);

		/* Check if L2 is held for strafe mode (left/right strafe instead of rotate) */
		if (canProcessInput) {
			strafeMode = (pad.buttons & PAD_L2) != 0;

			/* Force strafe mode when inside a house */
			if (gameState == STATE_INTERIOR) {
				strafeMode = true;
			}

			if (strafeMode) {
				/* Strafe mode: move relative to camera, left/right = strafe */
				int16_t strafeAngle = orbitAngle;
				if (gameState == STATE_INTERIOR) {
					strafeAngle = INTERIOR_CAMERA_ANGLE;
				}

				if (pad.isAnalog) {
					int stickX = (int)pad.leftX - 0x80;
					int stickY = (int)pad.leftY - 0x80;

					if (stickY < -ANALOG_DEADZONE) {
						strafeDirX -= isin(strafeAngle);
						strafeDirZ -= icos(strafeAngle);
					} else if (stickY > ANALOG_DEADZONE) {
						strafeDirX += isin(strafeAngle);
						strafeDirZ += icos(strafeAngle);
					}
					if (stickX < -ANALOG_DEADZONE) {
						int16_t leftAngle = strafeAngle + 1024;
						strafeDirX += isin(leftAngle);
						strafeDirZ += icos(leftAngle);
					} else if (stickX > ANALOG_DEADZONE) {
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

					int16_t targetFacing = iatan2(strafeDirX, strafeDirZ);
					int16_t diff = targetFacing - player.facing;
					while (diff > 2048) diff -= 4096;
					while (diff < -2048) diff += 4096;

					if (diff > ROTATION_THRESHOLD) moveX = 1;
					else if (diff < -ROTATION_THRESHOLD) moveX = -1;
				}
			} else {
				/* Normal mode: camera-relative movement */
				/* LEFT, RIGHT, L1, R1 all rotate camera */
				if ((pad.buttons & PAD_LEFT) || (pad.buttons & PAD_L1)) {
					orbitAngle -= PLAYER_TURN_SPEED;
				}
				if ((pad.buttons & PAD_RIGHT) || (pad.buttons & PAD_R1)) {
					orbitAngle += PLAYER_TURN_SPEED;
				}

				if (pad.buttons & PAD_UP) {
					int16_t cameraForward = orbitAngle + 2048;
					int16_t diff = cameraForward - player.facing;
					while (diff > 2048) diff -= 4096;
					while (diff < -2048) diff += 4096;

					if (diff > ROTATION_THRESHOLD) moveX = 1;
					else if (diff < -ROTATION_THRESHOLD) moveX = -1;

					int16_t absDiff = (diff < 0) ? -diff : diff;
					if (absDiff < 1024) moveZ = 1;
				}

				if (pad.buttons & PAD_DOWN) {
					int16_t diff = orbitAngle - player.facing;
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

					if (stickX > ANALOG_DEADZONE) orbitAngle += PLAYER_TURN_SPEED;
					else if (stickX < -ANALOG_DEADZONE) orbitAngle -= PLAYER_TURN_SPEED;

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

		/* Scene transition state machine (delta-time based) */
		if (gameState == STATE_FADE_OUT) {
			fadeAlpha += (FADE_SPEED * deltaTime) >> 8;
			if (fadeAlpha >= 255) {
				fadeAlpha = 255;
				if (transitionToInterior) {
					player.x = 0;
					player.y = PLAYER_Y_OFFSET << 12;
					player.z = 0;
					player.facing = 0;
					player.isWalking = false;

					/* If caught by enforcer, do the day reset now (screen is black) */
					if (caughtTransition) {
						caughtTransition = false;

						/* Advance to next day */
						currentDay++;
						maskDeliveredThisDay = false;
						if (currentDay > MAX_DAYS) {
							/* Game complete - show ending */
							introCharCount = 0;
							introTextComplete = false;
							gameState = STATE_ENDING;
							/* Skip the rest of the caught transition */
							goto skip_caught_reset;
						}

						/* Reset player arm position to resting */
						player.partRotX[PART_ARM_LEFT] = 0;
						player.partRotX[PART_ARM_RIGHT] = 0;
						player.partRotY[PART_ARM_LEFT] = 0;
						player.partRotY[PART_ARM_RIGHT] = 0;
						player.partRotZ[PART_ARM_LEFT] = 0;
						player.partRotZ[PART_ARM_RIGHT] = 0;

						/* Reset state for new day */
						hasFood = false;
						hasMask = false;
						foodBoxSpawned = false;
						momInstructionIndex = 0;
						instructionsDone = false;
						momCommentaryIndex = 0;
						talkedToMomAboutMasks = false;
						masksCollected = 0;
						correctFoodHouse = currentDay % NUM_HOUSES;

						/* Reset hiding adults for new day - avoid food delivery house! */
						for (int j = 0; j < NUM_HIDING_ADULTS; j++) {
							int candidateHouse = (j * 3 + currentDay) % NUM_HOUSES;
							if (candidateHouse == correctFoodHouse) {
								candidateHouse = (candidateHouse + 1) % NUM_HOUSES;
							}
							hidingAdults[j].houseIndex = candidateHouse;
							hidingAdults[j].hasMask = false;
							hidingAdultChars[j].x = HOUSE_ADULT_POS_X << 12;
							hidingAdultChars[j].y = HOUSE_ADULT_POS_Y << 12;
							hidingAdultChars[j].z = HOUSE_ADULT_POS_Z << 12;
						}

						/* Reset enforcers to patrol centers (now safe, screen is black) */
						for (int j = 0; j < MAX_ENFORCERS; j++) {
							enforcers[j].isActive = (j < currentDay);
							enforcers[j].state = ENFORCER_PATROL;
							enforcers[j].detectionMeter = 0;
							enforcers[j].x = enforcers[j].patrolCenterX << 12;
							enforcers[j].z = enforcers[j].patrolCenterZ << 12;
							enforcers[j].patrolWaypoint = 0;
							enforcers[j].waypointTimer = WAYPOINT_PAUSE_TIME;
						}
					}
				} else {
					player.x = entryPosX;
					player.z = entryPosZ;
					player.facing = entryFacing + 2048;
					orbitAngle = player.facing + 2048;
					player.isWalking = false;
					currentHouseIndex = -1;
				}
				gameState = STATE_BLACK;
				fadeHoldCounter = FADE_HOLD_FRAMES * 256;  /* Convert to delta units */
skip_caught_reset:;
			}
		} else if (gameState == STATE_BLACK) {
			fadeAlpha = 255;
			fadeHoldCounter -= deltaTime;
			if (fadeHoldCounter <= 0) {
				gameState = STATE_FADE_IN;
			}
		} else if (gameState == STATE_FADE_IN) {
			fadeAlpha -= (FADE_SPEED * deltaTime) >> 8;
			if (fadeAlpha <= 0) {
				fadeAlpha = 0;
				if (transitionToInterior) {
					/* Check if we need to show day intro */
					if (dayIntroTimer > 0) {
						gameState = STATE_DAY_INTRO;
					} else {
						gameState = STATE_INTERIOR;
					}
				} else {
					gameState = STATE_EXTERIOR;
					/* First time leaving restaurant - show credits */
					if (firstTimeLeaving) {
						firstTimeLeaving = false;
						showingCredits = true;
						creditsTimer = CREDITS_DURATION;
					}
				}
			}
		} else if (gameState == STATE_DAY_INTRO) {
			dayIntroTimer -= deltaTime;
			if (dayIntroTimer <= 0) {
				/* Day 5 special: Mom is gone, show farewell note */
				if (currentDay >= 5 && !instructionsDone) {
					instructionsDone = true;  /* Mark as done so player can leave */
					foodBoxSpawned = true;  /* Food box is on the floor */
					currentDialog = MOM_FAREWELL_NOTE;
					dialogCharCount = 0;
					dialogComplete = false;
					gameState = STATE_DIALOG;
				} else {
					gameState = STATE_INTERIOR;
				}
			}
		} else if (gameState == STATE_DIALOG) {
			if (currentDialog && !dialogComplete) {
				/* Typewriter effect using delta time accumulator */
				/* Advances one character every 512 accumulated (2 frames at 60fps) */
				dialogAccum += deltaTime;
				while (dialogAccum >= 512) {
					dialogAccum -= 512;
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

		/* Fade flash (delta-time based) */
		if (bgFlash > 0) {
			bgFlash -= (BG_FLASH_FADE_SPEED * deltaTime) >> 8;
			if (bgFlash < 0) bgFlash = 0;
		}

		/* Update CD-DA music - check if intro ended and switch to gameplay */
		updateCDDA();  /* Handle CD-DA state */
		if (currentMusic == MUSIC_INTRO && !isCDDAPlaying()) {
			/* Intro music ended - switch to gameplay loop */
			playCDDATrack(CDDA_TRACK_GAMEPLAY);
			currentMusic = MUSIC_GAMEPLAY;
		} else if (currentMusic == MUSIC_GAMEPLAY && !isCDDAPlaying()) {
			/* Gameplay loop ended - restart it */
			playCDDATrack(CDDA_TRACK_GAMEPLAY);
		}

		/* Update credits display timer */
		if (showingCredits) {
			creditsTimer--;
			if (creditsTimer <= 0) {
				showingCredits = false;
			}
		}

		/* Store old position for collision response */
		int32_t oldX = player.x;
		int32_t oldZ = player.z;

		/* Update character animation and movement */
		int walkSpeed = (gameState == STATE_INTERIOR) ? WALK_CYCLE_SPEED_INTERIOR : WALK_CYCLE_SPEED;
		updateCharacter(&player, moveX, moveZ, deltaTime, walkSpeed);

		/* In strafe mode, override movement */
		if (strafeMode && (strafeDirX != 0 || strafeDirZ != 0)) {
			player.x = oldX;
			player.z = oldZ;

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

		/* Play footstep SFX synced to walk cycle */
		if (player.isWalking) {
			/* Footsteps trigger when walk cycle crosses 0 or 2048 (each foot landing) */
			int16_t currentCycle = player.walkCycle;
			bool crossedZero = (lastWalkCycle > 3000 && currentCycle < 1000);
			bool crossedHalf = (lastWalkCycle < 2048 && currentCycle >= 2048);
			if (crossedZero || crossedHalf) {
				/* Play appropriate footstep sound based on location */
				if (gameState == STATE_INTERIOR) {
					/* Interior = wood floor */
					if (sfxStepWoodAddr) {
						playSample(SFX_CHANNEL_STEP, sfxStepWoodAddr, SFX_SAMPLE_RATE, FOOTSTEP_VOLUME);
					}
				} else {
					/* Exterior = grass */
					if (sfxStepGrassAddr) {
						playSample(SFX_CHANNEL_STEP, sfxStepGrassAddr, SFX_SAMPLE_RATE, FOOTSTEP_VOLUME);
					}
				}
			}
			lastWalkCycle = currentCycle;
		}

		/* Collision handling */
		if (gameState == STATE_EXTERIOR) {
			int32_t newWorldX = player.x >> 12;
			int32_t newWorldZ = player.z >> 12;
			int32_t oldWorldX = oldX >> 12;
			int32_t oldWorldZ = oldZ >> 12;

			bool hasCollision =
				checkAllHouseCollisions(newWorldX, newWorldZ, PLAYER_COLLISION_RADIUS, houses, NUM_HOUSES) ||
				checkHouseCollision(newWorldX, newWorldZ, PLAYER_COLLISION_RADIUS, &restaurant) ||
				checkAllTreeCollisions(newWorldX, newWorldZ, PLAYER_COLLISION_RADIUS, trees, NUM_MAP_TREES) ||
				checkAllFenceCollisions(newWorldX, newWorldZ, PLAYER_COLLISION_RADIUS);

			if (hasCollision) {
				bool canMoveX = !(
					checkAllHouseCollisions(newWorldX, oldWorldZ, PLAYER_COLLISION_RADIUS, houses, NUM_HOUSES) ||
					checkHouseCollision(newWorldX, oldWorldZ, PLAYER_COLLISION_RADIUS, &restaurant) ||
					checkAllTreeCollisions(newWorldX, oldWorldZ, PLAYER_COLLISION_RADIUS, trees, NUM_MAP_TREES) ||
					checkAllFenceCollisions(newWorldX, oldWorldZ, PLAYER_COLLISION_RADIUS));
				bool canMoveZ = !(
					checkAllHouseCollisions(oldWorldX, newWorldZ, PLAYER_COLLISION_RADIUS, houses, NUM_HOUSES) ||
					checkHouseCollision(oldWorldX, newWorldZ, PLAYER_COLLISION_RADIUS, &restaurant) ||
					checkAllTreeCollisions(oldWorldX, newWorldZ, PLAYER_COLLISION_RADIUS, trees, NUM_MAP_TREES) ||
					checkAllFenceCollisions(oldWorldX, newWorldZ, PLAYER_COLLISION_RADIUS));

				if (canMoveX && !canMoveZ) {
					player.z = oldZ;
				} else if (canMoveZ && !canMoveX) {
					player.x = oldX;
				} else {
					player.x = oldX;
					player.z = oldZ;
				}
			}
		} else if (gameState == STATE_INTERIOR) {
			int32_t floorHalfX, floorHalfZ;
			if (currentHouseIndex == NUM_HOUSES) {
				floorHalfX = RESTAURANT_INT_FLOOR_HALF_X;
				floorHalfZ = RESTAURANT_INT_FLOOR_HALF_Z;
			} else {
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

		/*====================================================================
		 * ENFORCER AI UPDATE (only in exterior)
		 *====================================================================*/
		maxDetectionLevel = 0;
		playerCaught = false;

		if (gameState == STATE_EXTERIOR) {
			int32_t playerWorldX = player.x >> 12;
			int32_t playerWorldZ = player.z >> 12;

			for (int i = 0; i < MAX_ENFORCERS; i++) {
				if (!enforcers[i].isActive) continue;

				int32_t enfWorldX = enforcers[i].x >> 12;
				int32_t enfWorldZ = enforcers[i].z >> 12;

				/* Calculate distance to player */
				int32_t dx = playerWorldX - enfWorldX;
				int32_t dz = playerWorldZ - enfWorldZ;
				int32_t distSq = dx * dx + dz * dz;

				/* Check if player in detection range */
				bool seesPlayer = false;
				if (distSq < DETECTION_RANGE * DETECTION_RANGE) {
					/* Check if player is in front (within cone) */
					int16_t angleToPlayer = iatan2(dx, dz);
					int16_t angleDiff = angleToPlayer - enforcers[i].facing;
					while (angleDiff > 2048) angleDiff -= 4096;
					while (angleDiff < -2048) angleDiff += 4096;
					if (angleDiff > -DETECTION_CONE && angleDiff < DETECTION_CONE) {
						seesPlayer = true;
					}
				}

				switch (enforcers[i].state) {
					case ENFORCER_PATROL:
						if (seesPlayer) {
							/* Switch to alert */
							enforcers[i].state = ENFORCER_ALERT;
							enforcers[i].detectionMeter += (DETECTION_RATE * deltaTime) >> 8;
						} else {
							/* Patrol movement: move toward current waypoint */
							int32_t waypointOffsetX = (enforcers[i].patrolWaypoint == 1 || enforcers[i].patrolWaypoint == 2) ? PATROL_HALF_SIZE : -PATROL_HALF_SIZE;
							int32_t waypointOffsetZ = (enforcers[i].patrolWaypoint == 0 || enforcers[i].patrolWaypoint == 1) ? PATROL_HALF_SIZE : -PATROL_HALF_SIZE;
							int32_t targetX = enforcers[i].patrolCenterX + waypointOffsetX;
							int32_t targetZ = enforcers[i].patrolCenterZ + waypointOffsetZ;

							int32_t wpDx = targetX - enfWorldX;
							int32_t wpDz = targetZ - enfWorldZ;
							int32_t wpDistSq = wpDx * wpDx + wpDz * wpDz;

							if (wpDistSq < 100 * 100) {
								/* At waypoint - pause then advance */
								enforcers[i].waypointTimer -= deltaTime;
								if (enforcers[i].waypointTimer <= 0) {
									enforcers[i].patrolWaypoint = (enforcers[i].patrolWaypoint + 1) % 4;
									enforcers[i].waypointTimer = WAYPOINT_PAUSE_TIME;
								}
							} else {
								/* Move toward waypoint */
								int16_t targetFacing = iatan2(wpDx, wpDz);
								int16_t turnDiff = targetFacing - enforcers[i].facing;
								while (turnDiff > 2048) turnDiff -= 4096;
								while (turnDiff < -2048) turnDiff += 4096;

								if (turnDiff > PLAYER_TURN_SPEED) enforcers[i].facing += PLAYER_TURN_SPEED;
								else if (turnDiff < -PLAYER_TURN_SPEED) enforcers[i].facing -= PLAYER_TURN_SPEED;
								else enforcers[i].facing = targetFacing;

								/* Move forward with collision checking */
								int32_t sinF = isin(enforcers[i].facing);
								int32_t cosF = icos(enforcers[i].facing);
								int32_t moveX = (sinF * ENFORCER_PATROL_SPEED) >> 12;
								int32_t moveZ = (cosF * ENFORCER_PATROL_SPEED) >> 12;
								int32_t newEnfX = enforcers[i].x + ((moveX * deltaTime) >> 8);
								int32_t newEnfZ = enforcers[i].z + ((moveZ * deltaTime) >> 8);
								int32_t newEnfWorldX = newEnfX >> 12;
								int32_t newEnfWorldZ = newEnfZ >> 12;

								/* Check collision at new position */
								bool enfCollision =
									checkAllHouseCollisions(newEnfWorldX, newEnfWorldZ, ENFORCER_COLLISION_RADIUS, houses, NUM_HOUSES) ||
									checkHouseCollision(newEnfWorldX, newEnfWorldZ, ENFORCER_COLLISION_RADIUS, &restaurant) ||
									checkAllTreeCollisions(newEnfWorldX, newEnfWorldZ, ENFORCER_COLLISION_RADIUS, trees, NUM_MAP_TREES) ||
									checkAllFenceCollisions(newEnfWorldX, newEnfWorldZ, ENFORCER_COLLISION_RADIUS);

								if (!enfCollision) {
									enforcers[i].x = newEnfX;
									enforcers[i].z = newEnfZ;
								} else {
									/* Hit obstacle - reverse direction (go to opposite waypoint) */
									enforcers[i].patrolWaypoint = (enforcers[i].patrolWaypoint + 2) % 4;
									enforcers[i].facing = (enforcers[i].facing + 2048) & 0xFFF;  /* Turn 180 degrees */
									enforcers[i].waypointTimer = WAYPOINT_PAUSE_TIME / 2;  /* Quick pause */
								}

								/* Animate walk */
								enforcers[i].walkCycle += (WALK_CYCLE_SPEED * deltaTime) >> 8;
								if (enforcers[i].walkCycle >= 4096) enforcers[i].walkCycle -= 4096;
							}
						}
						break;

					case ENFORCER_ALERT:
						if (seesPlayer) {
							/* Turn to face player */
							enforcers[i].facing = iatan2(dx, dz);
							/* Fill detection meter */
							enforcers[i].detectionMeter += (DETECTION_RATE * deltaTime) >> 8;
							if (enforcers[i].detectionMeter >= DETECTION_MAX) {
								enforcers[i].state = ENFORCER_CHASE;
								enforcers[i].cooldownTimer = CHASE_TIMEOUT;
							}
						} else {
							/* Decay detection meter */
							enforcers[i].detectionMeter -= (DETECTION_DECAY * deltaTime) >> 8;
							if (enforcers[i].detectionMeter <= 0) {
								enforcers[i].detectionMeter = 0;
								enforcers[i].state = ENFORCER_PATROL;
							}
						}
						break;

					case ENFORCER_CHASE:
						{
							/* Turn toward player */
							enforcers[i].facing = iatan2(dx, dz);

							/* Move toward player at chase speed with collision */
							int32_t sinF = isin(enforcers[i].facing);
							int32_t cosF = icos(enforcers[i].facing);
							int32_t moveX = (sinF * ENFORCER_CHASE_SPEED) >> 12;
							int32_t moveZ = (cosF * ENFORCER_CHASE_SPEED) >> 12;
							int32_t oldEnfX = enforcers[i].x;
							int32_t oldEnfZ = enforcers[i].z;
							int32_t newEnfX = oldEnfX + ((moveX * deltaTime) >> 8);
							int32_t newEnfZ = oldEnfZ + ((moveZ * deltaTime) >> 8);
							int32_t newEnfWorldX = newEnfX >> 12;
							int32_t newEnfWorldZ = newEnfZ >> 12;
							int32_t oldEnfWorldX = oldEnfX >> 12;
							int32_t oldEnfWorldZ = oldEnfZ >> 12;

							/* Check collision with sliding */
							bool enfCollision =
								checkAllHouseCollisions(newEnfWorldX, newEnfWorldZ, ENFORCER_COLLISION_RADIUS, houses, NUM_HOUSES) ||
								checkHouseCollision(newEnfWorldX, newEnfWorldZ, ENFORCER_COLLISION_RADIUS, &restaurant) ||
								checkAllTreeCollisions(newEnfWorldX, newEnfWorldZ, ENFORCER_COLLISION_RADIUS, trees, NUM_MAP_TREES) ||
								checkAllFenceCollisions(newEnfWorldX, newEnfWorldZ, ENFORCER_COLLISION_RADIUS);

							if (!enfCollision) {
								enforcers[i].x = newEnfX;
								enforcers[i].z = newEnfZ;
							} else {
								/* Try sliding along X axis only */
								bool canMoveX = !(
									checkAllHouseCollisions(newEnfWorldX, oldEnfWorldZ, ENFORCER_COLLISION_RADIUS, houses, NUM_HOUSES) ||
									checkHouseCollision(newEnfWorldX, oldEnfWorldZ, ENFORCER_COLLISION_RADIUS, &restaurant) ||
									checkAllTreeCollisions(newEnfWorldX, oldEnfWorldZ, ENFORCER_COLLISION_RADIUS, trees, NUM_MAP_TREES) ||
									checkAllFenceCollisions(newEnfWorldX, oldEnfWorldZ, ENFORCER_COLLISION_RADIUS));
								/* Try sliding along Z axis only */
								bool canMoveZ = !(
									checkAllHouseCollisions(oldEnfWorldX, newEnfWorldZ, ENFORCER_COLLISION_RADIUS, houses, NUM_HOUSES) ||
									checkHouseCollision(oldEnfWorldX, newEnfWorldZ, ENFORCER_COLLISION_RADIUS, &restaurant) ||
									checkAllTreeCollisions(oldEnfWorldX, newEnfWorldZ, ENFORCER_COLLISION_RADIUS, trees, NUM_MAP_TREES) ||
									checkAllFenceCollisions(oldEnfWorldX, newEnfWorldZ, ENFORCER_COLLISION_RADIUS));

								if (canMoveX && !canMoveZ) {
									enforcers[i].x = newEnfX;  /* Slide along X */
								} else if (canMoveZ && !canMoveX) {
									enforcers[i].z = newEnfZ;  /* Slide along Z */
								}
								/* If both blocked, don't move */
							}

							/* Faster walk animation */
							enforcers[i].walkCycle += (WALK_CYCLE_SPEED * 2 * deltaTime) >> 8;
							if (enforcers[i].walkCycle >= 4096) enforcers[i].walkCycle -= 4096;

							/* Check if caught player */
							if (distSq < CATCH_RADIUS * CATCH_RADIUS) {
								playerCaught = true;
							}

							/* Timeout if lost sight for too long */
							if (!seesPlayer) {
								enforcers[i].cooldownTimer -= deltaTime;
								if (enforcers[i].cooldownTimer <= 0) {
									enforcers[i].state = ENFORCER_PATROL;
									enforcers[i].detectionMeter = 0;
								}
							} else {
								enforcers[i].cooldownTimer = CHASE_TIMEOUT;
							}
						}
						break;
				}

				/* Track max detection for UI */
				if (enforcers[i].detectionMeter > maxDetectionLevel) {
					maxDetectionLevel = enforcers[i].detectionMeter;
				}
			}

			/* Handle player caught - start fade, defer reset until screen is black */
			if (playerCaught) {
				/* Start transition - actual reset happens in STATE_BLACK */
				caughtTransition = true;
				transitionToInterior = true;
				currentHouseIndex = NUM_HOUSES;
				dayIntroTimer = DAY_INTRO_DURATION;
				gameState = STATE_FADE_OUT;
				fadeAlpha = 0;
			}
		}

		/* Update mom in restaurant interior */
		bool nearMom = false;
		if (gameState == STATE_INTERIOR && currentHouseIndex == NUM_HOUSES) {
			int32_t momDistX = (player.x >> 12) - MOM_POS_X;
			int32_t momDistZ = (player.z >> 12) - MOM_POS_Z;
			int32_t momDistSq = momDistX * momDistX + momDistZ * momDistZ;
			nearMom = (momDistSq < INTERACT_RADIUS * INTERACT_RADIUS);

			if (nearMom) {
				int16_t angleToPlayer = (int16_t)iatan2(momDistX, momDistZ);
				int16_t angleDiff = angleToPlayer - mom.facing;
				while (angleDiff > 2048) angleDiff -= 4096;
				while (angleDiff < -2048) angleDiff += 4096;
				if (angleDiff > PLAYER_TURN_SPEED) mom.facing += PLAYER_TURN_SPEED;
				else if (angleDiff < -PLAYER_TURN_SPEED) mom.facing -= PLAYER_TURN_SPEED;
				else mom.facing = angleToPlayer;
			}

			updateCharacter(&mom, 0, 0, deltaTime, WALK_CYCLE_SPEED_INTERIOR);

			/* Update hiding adult - turn to face player and idle animation */
			for (int i = 0; i < NUM_HIDING_ADULTS; i++) {
				if (hidingAdults[i].houseIndex == NUM_HOUSES) {
					int32_t adultDistX = (player.x >> 12) - (hidingAdultChars[i].x >> 12);
					int32_t adultDistZ = (player.z >> 12) - (hidingAdultChars[i].z >> 12);
					int32_t adultDistSq = adultDistX * adultDistX + adultDistZ * adultDistZ;
					if (adultDistSq < INTERACT_RADIUS * INTERACT_RADIUS * 4) {
						/* Turn to face player */
						int16_t angleToPlayer = (int16_t)iatan2(adultDistX, adultDistZ);
						int16_t angleDiff = angleToPlayer - hidingAdultChars[i].facing;
						while (angleDiff > 2048) angleDiff -= 4096;
						while (angleDiff < -2048) angleDiff += 4096;
						if (angleDiff > PLAYER_TURN_SPEED) hidingAdultChars[i].facing += PLAYER_TURN_SPEED;
						else if (angleDiff < -PLAYER_TURN_SPEED) hidingAdultChars[i].facing -= PLAYER_TURN_SPEED;
						else hidingAdultChars[i].facing = angleToPlayer;
					}
					updateCharacter(&hidingAdultChars[i], 0, 0, deltaTime, WALK_CYCLE_SPEED_INTERIOR);
				}
			}

#if DEBUG_CHARACTERS
			/* Update restaurant citizens - turn to face player and idle animation (debug only) */
			for (int i = 0; i < NUM_RESTAURANT_CITIZENS; i++) {
				int32_t citizenDistX = (player.x >> 12) - (restaurantCitizens[i].x >> 12);
				int32_t citizenDistZ = (player.z >> 12) - (restaurantCitizens[i].z >> 12);
				int32_t citizenDistSq = citizenDistX * citizenDistX + citizenDistZ * citizenDistZ;
				if (citizenDistSq < INTERACT_RADIUS * INTERACT_RADIUS * 4) {
					/* Turn to face player */
					int16_t angleToPlayer = (int16_t)iatan2(citizenDistX, citizenDistZ);
					int16_t angleDiff = angleToPlayer - restaurantCitizens[i].facing;
					while (angleDiff > 2048) angleDiff -= 4096;
					while (angleDiff < -2048) angleDiff += 4096;
					if (angleDiff > PLAYER_TURN_SPEED) restaurantCitizens[i].facing += PLAYER_TURN_SPEED;
					else if (angleDiff < -PLAYER_TURN_SPEED) restaurantCitizens[i].facing -= PLAYER_TURN_SPEED;
					else restaurantCitizens[i].facing = angleToPlayer;
				}
				updateCharacter(&restaurantCitizens[i], 0, 0, deltaTime, WALK_CYCLE_SPEED_INTERIOR);
			}
#endif
		}

		/* Update house citizens when inside a regular house */
		if (gameState == STATE_INTERIOR && currentHouseIndex >= 0 && currentHouseIndex < NUM_HOUSES) {
			/* Check if an adult is in this house */
			bool hasAdultInHouse = false;
			for (int i = 0; i < NUM_HIDING_ADULTS; i++) {
				if (hidingAdults[i].houseIndex == currentHouseIndex) {
					hasAdultInHouse = true;
					break;
				}
			}

			/* Update citizens (max 2 total, so only 1 if adult present) */
			int numCitizens = hasAdultInHouse ? 1 : NUM_CITIZENS_PER_HOUSE;
			for (int i = 0; i < numCitizens; i++) {
				int32_t citizenDistX = (player.x >> 12) - (houseCitizens[currentHouseIndex][i].x >> 12);
				int32_t citizenDistZ = (player.z >> 12) - (houseCitizens[currentHouseIndex][i].z >> 12);
				int32_t citizenDistSq = citizenDistX * citizenDistX + citizenDistZ * citizenDistZ;
				if (citizenDistSq < INTERACT_RADIUS * INTERACT_RADIUS * 4) {
					/* Turn to face player */
					int16_t angleToPlayer = (int16_t)iatan2(citizenDistX, citizenDistZ);
					int16_t angleDiff = angleToPlayer - houseCitizens[currentHouseIndex][i].facing;
					while (angleDiff > 2048) angleDiff -= 4096;
					while (angleDiff < -2048) angleDiff += 4096;
					if (angleDiff > PLAYER_TURN_SPEED) houseCitizens[currentHouseIndex][i].facing += PLAYER_TURN_SPEED;
					else if (angleDiff < -PLAYER_TURN_SPEED) houseCitizens[currentHouseIndex][i].facing -= PLAYER_TURN_SPEED;
					else houseCitizens[currentHouseIndex][i].facing = angleToPlayer;
				}
				updateCharacter(&houseCitizens[currentHouseIndex][i], 0, 0, deltaTime, WALK_CYCLE_SPEED_INTERIOR);
			}

			/* Update hiding adult if in this house */
			for (int i = 0; i < NUM_HIDING_ADULTS; i++) {
				if (hidingAdults[i].houseIndex == currentHouseIndex) {
					int32_t adultDistX = (player.x >> 12) - (hidingAdultChars[i].x >> 12);
					int32_t adultDistZ = (player.z >> 12) - (hidingAdultChars[i].z >> 12);
					int32_t adultDistSq = adultDistX * adultDistX + adultDistZ * adultDistZ;
					if (adultDistSq < INTERACT_RADIUS * INTERACT_RADIUS * 4) {
						int16_t angleToPlayer = (int16_t)iatan2(adultDistX, adultDistZ);
						int16_t angleDiff = angleToPlayer - hidingAdultChars[i].facing;
						while (angleDiff > 2048) angleDiff -= 4096;
						while (angleDiff < -2048) angleDiff += 4096;
						if (angleDiff > PLAYER_TURN_SPEED) hidingAdultChars[i].facing += PLAYER_TURN_SPEED;
						else if (angleDiff < -PLAYER_TURN_SPEED) hidingAdultChars[i].facing -= PLAYER_TURN_SPEED;
						else hidingAdultChars[i].facing = angleToPlayer;
					}
					updateCharacter(&hidingAdultChars[i], 0, 0, deltaTime, WALK_CYCLE_SPEED_INTERIOR);
				}
			}
		}

		/* Check if near food box in restaurant */
		bool nearFoodBox = false;
		if (gameState == STATE_INTERIOR && currentHouseIndex == NUM_HOUSES && foodBoxSpawned && !hasFood) {
			int32_t foodDistX = (player.x >> 12) - FOOD_BOX_POS_X;
			int32_t foodDistZ = (player.z >> 12) - FOOD_BOX_POS_Z;
			int32_t foodDistSq = foodDistX * foodDistX + foodDistZ * foodDistZ;
			nearFoodBox = (foodDistSq < INTERACT_RADIUS * INTERACT_RADIUS);
		}

		/* Check if near house NPC for interaction prompt */
		bool nearHouseNPC = false;
		bool nearMomForPrompt = false;
		if (gameState == STATE_INTERIOR && currentHouseIndex == NUM_HOUSES) {
			/* In restaurant - check mom proximity for any valid interaction */
			int32_t momDistX = (player.x >> 12) - MOM_POS_X;
			int32_t momDistZ = (player.z >> 12) - MOM_POS_Z;
			int32_t momDistSq = momDistX * momDistX + momDistZ * momDistZ;
			bool inRange = (momDistSq < INTERACT_RADIUS * INTERACT_RADIUS);
			/* Player can ALWAYS talk to mom on days 1-4 when in range */
			bool canTalk = (currentDay < 5);
			nearMomForPrompt = inRange && canTalk;
		} else if (gameState == STATE_INTERIOR && currentHouseIndex >= 0 && currentHouseIndex < NUM_HOUSES) {
			/* In regular house - check if near citizen (for food delivery) or hiding adult (for mask) */
			/* Check if an adult is in this house to limit citizen count */
			bool hasAdultHere = false;
			for (int a = 0; a < NUM_HIDING_ADULTS; a++) {
				if (hidingAdults[a].houseIndex == currentHouseIndex) {
					hasAdultHere = true;
					break;
				}
			}
			int numCitizensHere = hasAdultHere ? 1 : NUM_CITIZENS_PER_HOUSE;

			if (hasFood) {
				/* Near any citizen to deliver food */
				for (int i = 0; i < numCitizensHere; i++) {
					int32_t distX = (player.x >> 12) - (houseCitizens[currentHouseIndex][i].x >> 12);
					int32_t distZ = (player.z >> 12) - (houseCitizens[currentHouseIndex][i].z >> 12);
					int32_t distSq = distX * distX + distZ * distZ;
					if (distSq < INTERACT_RADIUS * INTERACT_RADIUS) {
						nearHouseNPC = true;
						break;
					}
				}
			} else if (hasMask) {
				/* Near hiding adult to give mask */
				for (int i = 0; i < NUM_HIDING_ADULTS; i++) {
					if (hidingAdults[i].houseIndex == currentHouseIndex && !hidingAdults[i].hasMask) {
						int32_t distX = (player.x >> 12) - (hidingAdultChars[i].x >> 12);
						int32_t distZ = (player.z >> 12) - (hidingAdultChars[i].z >> 12);
						int32_t distSq = distX * distX + distZ * distZ;
						if (distSq < INTERACT_RADIUS * INTERACT_RADIUS) {
							nearHouseNPC = true;
							break;
						}
					}
				}
			}
		}

		/* Check door triggers */
		int triggeredDoor = -1;
		bool atInteriorExit = false;

		if (gameState == STATE_EXTERIOR) {
			triggeredDoor = checkDoorTrigger(player.x >> 12, player.z >> 12, houses, NUM_HOUSES);
			if (triggeredDoor < 0 && isAtBuildingDoor(player.x >> 12, player.z >> 12, &restaurant)) {
				triggeredDoor = NUM_HOUSES;
			}
		} else if (gameState == STATE_INTERIOR && currentHouseIndex >= 0) {
			int32_t playerLocalX = player.x >> 12;
			int32_t playerLocalZ = player.z >> 12;

			int32_t doorX, doorZ, doorSizeX, doorSizeZ;
			if (currentHouseIndex == NUM_HOUSES) {
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

			if (playerLocalX >= doorX - doorSizeX && playerLocalX <= doorX + doorSizeX &&
				playerLocalZ >= doorZ - doorSizeZ && playerLocalZ <= doorZ + doorSizeZ) {
				atInteriorExit = true;
			}
		}

		/* X button handling */
		if ((pad.buttons & PAD_X) && !(prevButtons & PAD_X)) {
			if (gameState == STATE_INTRO_1) {
				if (!introTextComplete) {
					introTextComplete = true;
					introCharCount = 9999;
				} else {
					gameState = STATE_INTRO_2;
					introCharCount = 0;
					introTextComplete = false;
				}
			} else if (gameState == STATE_INTRO_2) {
				if (!introTextComplete) {
					introTextComplete = true;
					introCharCount = 9999;
				} else {
					gameState = STATE_DAY_INTRO;
					dayIntroTimer = DAY_INTRO_DURATION;
					currentHouseIndex = NUM_HOUSES;
					player.x = 0;
					player.y = PLAYER_Y_OFFSET << 12;
					player.z = 0;
					player.facing = 0;
					player.isWalking = false;
					entryPosX = (PLAYER_SPAWN_X + PLAYER_SPAWN_OFFSET_X) << 12;
					entryPosZ = (PLAYER_SPAWN_Z + PLAYER_SPAWN_OFFSET_Z) << 12;
					entryFacing = 2048;
				}
			} else if (gameState == STATE_DAY_INTRO) {
				dayIntroTimer = 0;  /* Reset timer when skipping day intro */
				gameState = STATE_INTERIOR;
			} else if (gameState == STATE_DIALOG) {
				if (!dialogComplete) {
					dialogComplete = true;
					dialogCharCount = 9999;
				} else {
					gameState = STATE_INTERIOR;
					currentDialog = NULL;
					if (instructionsDone && !foodBoxSpawned) {
						foodBoxSpawned = true;
						targetHouseIndex = correctFoodHouse;
					}
				}
			} else if (gameState == STATE_ENDING) {
				if (!introTextComplete) {
					introTextComplete = true;
					introCharCount = 9999;
				} else {
					gameState = STATE_ENDING_2;
					introCharCount = 0;
					introTextComplete = false;
				}
			} else if (gameState == STATE_ENDING_2) {
				if (!introTextComplete) {
					introTextComplete = true;
					introCharCount = 9999;
				} else {
					/* Return to title screen */
					gameState = STATE_TITLE;
					currentDay = 1;
				}
			} else if (gameState == STATE_EXTERIOR && triggeredDoor >= 0) {
				entryPosX = player.x;
				entryPosZ = player.z;
				entryFacing = player.facing;
				currentHouseIndex = triggeredDoor;
				transitionToInterior = true;
				dayIntroTimer = 0;  /* Don't show day intro when entering a house */
				gameState = STATE_FADE_OUT;
				fadeAlpha = 0;

				/* Reset all enforcers when entering a house (escape mechanic) */
				for (int i = 0; i < MAX_ENFORCERS; i++) {
					enforcers[i].state = ENFORCER_PATROL;
					enforcers[i].detectionMeter = 0;
					enforcers[i].x = enforcers[i].patrolCenterX << 12;
					enforcers[i].z = enforcers[i].patrolCenterZ << 12;
					enforcers[i].patrolWaypoint = 0;
					enforcers[i].waypointTimer = WAYPOINT_PAUSE_TIME;
				}
			} else if (gameState == STATE_INTERIOR && currentHouseIndex == NUM_HOUSES) {
				bool handledInteraction = false;

				/* Talk to mom - first about food, then about masks */
				int32_t momDistX = (player.x >> 12) - MOM_POS_X;
				int32_t momDistZ = (player.z >> 12) - MOM_POS_Z;
				int32_t momDistSq = momDistX * momDistX + momDistZ * momDistZ;
				bool nearMomForTalk = momDistSq < INTERACT_RADIUS * INTERACT_RADIUS;

				if (!instructionsDone && nearMomForTalk && currentDay < 5) {
					/* Show instruction lines until all done, then food spawns */
					int instructionCount = 1;
					switch (currentDay) {
						case 1: instructionCount = MOM_DIALOG_DAY1_COUNT; break;
						case 2: instructionCount = MOM_DIALOG_DAY2_COUNT; break;
						case 3: instructionCount = MOM_DIALOG_DAY3_COUNT; break;
						case 4: instructionCount = MOM_DIALOG_DAY4_COUNT; break;
					}

					/* Select the right instruction line for this day */
					if (currentDay == 1) {
						if (momInstructionIndex == 0) currentDialog = MOM_DIALOG_DAY1_0;
						else currentDialog = MOM_DIALOG_DAY1_1;
					} else if (currentDay == 2) {
						currentDialog = MOM_DIALOG_DAY2_0;
					} else if (currentDay == 3) {
						currentDialog = MOM_DIALOG_DAY3_0;
					} else {
						currentDialog = MOM_DIALOG_DAY4_0;
					}

					momInstructionIndex++;
					if (momInstructionIndex >= instructionCount) {
						instructionsDone = true;
					}

					dialogCharCount = 0;
					dialogComplete = false;
					gameState = STATE_DIALOG;
					handledInteraction = true;
				} else if (instructionsDone && !talkedToMomAboutMasks && masksCollected > 0 && nearMomForTalk) {
					/* After getting first mask, mom tells about distributing them */
					talkedToMomAboutMasks = true;
					currentDialog = MOM_MASK_DIALOG;
					dialogCharCount = 0;
					dialogComplete = false;
					gameState = STATE_DIALOG;
					handledInteraction = true;
				} else if (instructionsDone && !hasFood && !hasMask && !foodBoxSpawned &&
				           masksCollected < NUM_HIDING_ADULTS) {
					/* Day 5: Food auto-spawns on floor, no mom dialog */
					if (currentDay >= 5) {
						foodBoxSpawned = true;
						handledInteraction = true;
					} else if (nearMomForTalk) {
						/* Days 1-4: Player delivered a mask and needs more food from mom */
						currentDialog = MOM_MORE_FOOD_DIALOG;
						dialogCharCount = 0;
						dialogComplete = false;
						gameState = STATE_DIALOG;
						foodBoxSpawned = true;
						handledInteraction = true;
					}
				} else if (nearMomForTalk && currentDay < 5 && foodBoxSpawned) {
					/* Optional cycling commentary after food spawns */
					int commentCount = 1;
					switch (currentDay) {
						case 1: commentCount = MOM_COMMENT_DAY1_COUNT; break;
						case 2: commentCount = MOM_COMMENT_DAY2_COUNT; break;
						case 3: commentCount = MOM_COMMENT_DAY3_COUNT; break;
						case 4: commentCount = MOM_COMMENT_DAY4_COUNT; break;
					}

					/* Cycle through commentary lines */
					int idx = momCommentaryIndex % commentCount;
					if (currentDay == 1) {
						if (idx == 0) currentDialog = MOM_COMMENT_DAY1_0;
						else if (idx == 1) currentDialog = MOM_COMMENT_DAY1_1;
						else currentDialog = MOM_COMMENT_DAY1_2;
					} else if (currentDay == 2) {
						if (idx == 0) currentDialog = MOM_COMMENT_DAY2_0;
						else if (idx == 1) currentDialog = MOM_COMMENT_DAY2_1;
						else currentDialog = MOM_COMMENT_DAY2_2;
					} else if (currentDay == 3) {
						if (idx == 0) currentDialog = MOM_COMMENT_DAY3_0;
						else currentDialog = MOM_COMMENT_DAY3_1;
					} else {
						if (idx == 0) currentDialog = MOM_COMMENT_DAY4_0;
						else if (idx == 1) currentDialog = MOM_COMMENT_DAY4_1;
						else currentDialog = MOM_COMMENT_DAY4_2;
					}

					momCommentaryIndex++;
					dialogCharCount = 0;
					dialogComplete = false;
					gameState = STATE_DIALOG;
					handledInteraction = true;
				}

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

#if DEBUG_CHARACTERS
				/* Debug: allow giving masks to adults in restaurant */
				if (!handledInteraction && hasMask) {
					for (int i = 0; i < NUM_HIDING_ADULTS; i++) {
						if (hidingAdults[i].houseIndex == NUM_HOUSES && !hidingAdults[i].hasMask) {
							/* Check if near this adult */
							int32_t adultDistX = (player.x >> 12) - (hidingAdultChars[i].x >> 12);
							int32_t adultDistZ = (player.z >> 12) - (hidingAdultChars[i].z >> 12);
							int32_t adultDistSq = adultDistX * adultDistX + adultDistZ * adultDistZ;
							if (adultDistSq < INTERACT_RADIUS * INTERACT_RADIUS) {
								/* Give mask to this adult */
								currentDialog = ADULT_ACCEPT_MASK;
								dialogCharCount = 0;
								dialogComplete = false;
								gameState = STATE_DIALOG;
								hidingAdults[i].hasMask = true;
								hasMask = false;
								player.isCarrying = false;
								foodBoxSpawned = false; /* Allow getting more food from mom */
								handledInteraction = true;
								break;
							}
						}
					}
				}
#endif

				if (!handledInteraction && atInteriorExit) {
					if (!instructionsDone) {
						currentDialog = NEED_TO_TALK_MSG;
						dialogCharCount = 0;
						dialogComplete = false;
						gameState = STATE_DIALOG;
					} else if (!hasFood) {
						currentDialog = NEED_FOOD_MSG;
						dialogCharCount = 0;
						dialogComplete = false;
						gameState = STATE_DIALOG;
					} else {
						transitionToInterior = false;
						gameState = STATE_FADE_OUT;
						fadeAlpha = 0;
					}
				}
			} else if (gameState == STATE_INTERIOR && currentHouseIndex >= 0 && currentHouseIndex < NUM_HOUSES) {
				/* Inside a regular house - handle exit, food delivery, and mask giving */
				bool handledInteraction = false;

				/* Handle exit FIRST - prioritize leaving when at door */
				if (atInteriorExit) {
					/* Check if day should end (mask delivered this day) */
					if (maskDeliveredThisDay) {
						/* Day ends - advance to next day */
						currentDay++;
						maskDeliveredThisDay = false;

						if (currentDay > MAX_DAYS) {
							/* Game complete - show ending! */
							introCharCount = 0;
							introTextComplete = false;
							gameState = STATE_ENDING;
							handledInteraction = true;
						} else {
							/* Reset state for new day */
							hasFood = false;
							hasMask = false;
							momInstructionIndex = 0;
							momCommentaryIndex = 0;
							talkedToMomAboutMasks = false;
							masksCollected = 0;
							correctFoodHouse = currentDay % NUM_HOUSES;

							/* Day 5: Mom is gone, food auto-spawns on floor */
							if (currentDay >= 5) {
								foodBoxSpawned = true;
								instructionsDone = true;
							} else {
								foodBoxSpawned = false;
								instructionsDone = false;
							}

							/* Reset hiding adults for new day - avoid food delivery house! */
							for (int i = 0; i < NUM_HIDING_ADULTS; i++) {
								int candidateHouse = (i * 3 + currentDay) % NUM_HOUSES;
								if (candidateHouse == correctFoodHouse) {
									candidateHouse = (candidateHouse + 1) % NUM_HOUSES;
								}
								hidingAdults[i].houseIndex = candidateHouse;
								hidingAdults[i].hasMask = false;
								hidingAdultChars[i].x = HOUSE_ADULT_POS_X << 12;
								hidingAdultChars[i].y = HOUSE_ADULT_POS_Y << 12;
								hidingAdultChars[i].z = HOUSE_ADULT_POS_Z << 12;
							}

							/* Activate enforcers for new day (Day N = N enforcers) */
							for (int i = 0; i < MAX_ENFORCERS; i++) {
								enforcers[i].isActive = (i < currentDay);
								enforcers[i].state = ENFORCER_PATROL;
								enforcers[i].detectionMeter = 0;
								enforcers[i].x = enforcers[i].patrolCenterX << 12;
								enforcers[i].z = enforcers[i].patrolCenterZ << 12;
								enforcers[i].patrolWaypoint = 0;
								enforcers[i].waypointTimer = WAYPOINT_PAUSE_TIME;
							}

							/* Reset entry position to restaurant exterior */
							entryPosX = (PLAYER_SPAWN_X + PLAYER_SPAWN_OFFSET_X) << 12;
							entryPosZ = (PLAYER_SPAWN_Z + PLAYER_SPAWN_OFFSET_Z) << 12;
							entryFacing = 2048;

							/* Transition to restaurant and show day intro */
							transitionToInterior = true;
							currentHouseIndex = NUM_HOUSES;  /* Go to restaurant */
							dayIntroTimer = DAY_INTRO_DURATION;
							gameState = STATE_FADE_OUT;
							fadeAlpha = 0;
						}
					} else {
						/* Normal exit - just leave the house */
						transitionToInterior = false;
						gameState = STATE_FADE_OUT;
						fadeAlpha = 0;
					}
					handledInteraction = true;
				}

				/* If player has food, try to deliver it (only if not exiting) */
				if (!handledInteraction && hasFood) {
					if (currentHouseIndex == correctFoodHouse) {
						/* Correct house - accept food, give mask */
						currentDialog = CITIZEN_ACCEPT_FOOD;
						dialogCharCount = 0;
						dialogComplete = false;
						gameState = STATE_DIALOG;
						hasFood = false;
						hasMask = true;
						player.isCarrying = true;  /* Still carrying the mask */
						masksCollected++;
						handledInteraction = true;
						/* Set new target for next delivery */
						correctFoodHouse = (correctFoodHouse + 1) % NUM_HOUSES;
						targetHouseIndex = correctFoodHouse;
					} else {
						/* Wrong house - reject food */
						currentDialog = CITIZEN_REJECT_FOOD;
						dialogCharCount = 0;
						dialogComplete = false;
						gameState = STATE_DIALOG;
						handledInteraction = true;
					}
				}

				/* If player has mask, check for hiding adults */
				if (!handledInteraction && hasMask) {
					for (int i = 0; i < NUM_HIDING_ADULTS; i++) {
						if (hidingAdults[i].houseIndex == currentHouseIndex && !hidingAdults[i].hasMask) {
							/* Give mask to this adult */
							currentDialog = ADULT_ACCEPT_MASK;
							dialogCharCount = 0;
							dialogComplete = false;
							gameState = STATE_DIALOG;
							hidingAdults[i].hasMask = true;
							hasMask = false;
							player.isCarrying = false;
							foodBoxSpawned = false; /* Allow getting more food from mom */
							maskDeliveredThisDay = true;  /* Mark day as complete */
							handledInteraction = true;
							break;
						}
					}
				}
			} else if (gameState == STATE_INTERIOR && atInteriorExit) {
				transitionToInterior = false;
				gameState = STATE_FADE_OUT;
				fadeAlpha = 0;
			}
		}

		/* Start button handling */
		if ((pad.buttons & PAD_START) && !(prevButtons & PAD_START)) {
			if (gameState == STATE_TITLE) {
				gameState = STATE_INTRO_1;
				introCharCount = 0;
				introTextComplete = false;
				/* Start intro music (CD-DA track 2) */
				if (currentMusic != MUSIC_INTRO) {
					playCDDATrack(CDDA_TRACK_INTRO);
					currentMusic = MUSIC_INTRO;
				}
			} else if (gameState == STATE_INTRO_1) {
				if (!introTextComplete) {
					introTextComplete = true;
					introCharCount = 9999;
				} else {
					gameState = STATE_INTRO_2;
					introCharCount = 0;
					introTextComplete = false;
				}
			} else if (gameState == STATE_INTRO_2) {
				if (!introTextComplete) {
					introTextComplete = true;
					introCharCount = 9999;
				} else {
					gameState = STATE_DAY_INTRO;
					dayIntroTimer = DAY_INTRO_DURATION;
					currentHouseIndex = NUM_HOUSES;
					player.x = 0;
					player.y = PLAYER_Y_OFFSET << 12;
					player.z = 0;
					player.facing = 0;
					player.isWalking = false;
					entryPosX = (PLAYER_SPAWN_X + PLAYER_SPAWN_OFFSET_X) << 12;
					entryPosZ = (PLAYER_SPAWN_Z + PLAYER_SPAWN_OFFSET_Z) << 12;
					entryFacing = 2048;
				}
			} else if (gameState == STATE_DAY_INTRO) {
				dayIntroTimer = 0;  /* Reset timer when skipping day intro */
				gameState = STATE_INTERIOR;
			} else if (gameState == STATE_PAUSED) {
				gameState = prePauseState;
			} else if (gameState == STATE_EXTERIOR || gameState == STATE_INTERIOR) {
				prePauseState = gameState;
				gameState = STATE_PAUSED;
			}
		}

		/* DEBUG: Select button skips to next day */
#if DEBUG_SKIP_DAY
		if ((pad.buttons & PAD_SELECT) && !(prevButtons & PAD_SELECT)) {
			if (gameState == STATE_EXTERIOR || gameState == STATE_INTERIOR) {
				/* Advance to next day */
				currentDay++;
				if (currentDay > MAX_DAYS) {
					/* Game complete - show ending */
					introCharCount = 0;
					introTextComplete = false;
					gameState = STATE_ENDING;
				} else {
					/* Reset state for new day */
					hasFood = false;
					hasMask = false;
					momInstructionIndex = 0;
					momCommentaryIndex = 0;
					talkedToMomAboutMasks = false;
					masksCollected = 0;
					maskDeliveredThisDay = false;
					correctFoodHouse = currentDay % NUM_HOUSES;
					player.isCarrying = false;

					/* Day 5: Mom is gone, food auto-spawns on floor */
					if (currentDay >= 5) {
						foodBoxSpawned = true;
						instructionsDone = true;
					} else {
						foodBoxSpawned = false;
						instructionsDone = false;
					}

					/* Reset hiding adults for new day */
					for (int i = 0; i < NUM_HIDING_ADULTS; i++) {
						int candidateHouse = (i * 3 + currentDay) % NUM_HOUSES;
						if (candidateHouse == correctFoodHouse) {
							candidateHouse = (candidateHouse + 1) % NUM_HOUSES;
						}
						hidingAdults[i].houseIndex = candidateHouse;
						hidingAdults[i].hasMask = false;
					}

					/* Reset enforcers */
					for (int i = 0; i < MAX_ENFORCERS; i++) {
						enforcers[i].isActive = (i < currentDay);
						enforcers[i].state = ENFORCER_PATROL;
						enforcers[i].detectionMeter = 0;
						enforcers[i].x = enforcers[i].patrolCenterX << 12;
						enforcers[i].z = enforcers[i].patrolCenterZ << 12;
						enforcers[i].patrolWaypoint = 0;
						enforcers[i].waypointTimer = WAYPOINT_PAUSE_TIME;
					}

					/* Go to restaurant with day intro */
					player.x = 0;
					player.y = PLAYER_Y_OFFSET << 12;
					player.z = 0;
					player.facing = 0;
					currentHouseIndex = NUM_HOUSES;
					dayIntroTimer = DAY_INTRO_DURATION;
					gameState = STATE_DAY_INTRO;
				}
			}
		}
#endif

		prevButtons = pad.buttons;
		frameCounter++;

		/* Check if we're in menu/intro state */
		bool inMenuState = (gameState == STATE_TITLE || gameState == STATE_INTRO_1 ||
			gameState == STATE_INTRO_2 || gameState == STATE_DAY_INTRO ||
			gameState == STATE_ENDING || gameState == STATE_ENDING_2);

		if (inMenuState) {
			/* Title / Intro rendering */
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
				/* Draw title screen image (256x256 centered on 320x240 screen) */
				int imgX = (SCREEN_WIDTH - TITLE_TEX_WIDTH) / 2;  /* 32 */
				int imgY = 0;  /* Top of screen, bottom will be clipped */

				/* Draw title image: texpage + rectangle in same packet for guaranteed adjacency */
				/* Using OT index 2 so it's behind text at 0/1 */
				ptr = allocatePacket(chain, 2, 5);
				ptr[0] = gp0_texpage(titleTex.page, false, false);
				ptr[1] = gp0_rectangle(true, false, false) | gp0_rgb(128, 128, 128);
				ptr[2] = gp0_xy(imgX, imgY);
				ptr[3] = gp0_uv(titleTex.u, titleTex.v, 0);
				ptr[4] = gp0_xy(TITLE_TEX_WIDTH, TITLE_TEX_HEIGHT);

				/* Draw "Press START" prompt below the image (always visible, blinking) */
				const char *prompt = "Press [START] to play";
				int promptX = (SCREEN_WIDTH - 21 * 5) / 2;
				int promptY = 220;

				/* Draw text every frame but make it blink by changing color */
				if ((frameCounter / 30) % 2 == 0) {
					printStringColorZ(chain, &font, promptX + 1, promptY + 1, prompt, 20, 20, 40, 1);
					printStringColorZ(chain, &font, promptX, promptY, prompt, 100, 150, 255, 0);
				} else {
					printStringColorZ(chain, &font, promptX + 1, promptY + 1, prompt, 15, 15, 30, 1);
					printStringColorZ(chain, &font, promptX, promptY, prompt, 70, 100, 180, 0);
				}
			} else if (gameState == STATE_INTRO_1 || gameState == STATE_INTRO_2) {
				const char *text = (gameState == STATE_INTRO_1) ? INTRO_QUOTE : INTRO_STORY;
				int textLen = 0;
				for (const char *p = text; *p; p++) textLen++;

				if (!introTextComplete && (frameCounter % 2 == 0)) {
					introCharCount += INTRO_TEXT_SPEED;
					if (introCharCount >= textLen) {
						introCharCount = textLen;
						introTextComplete = true;
					}
				}

				char displayText[512];
				int i;
				for (i = 0; i < introCharCount && i < 511 && text[i]; i++) {
					displayText[i] = text[i];
				}
				displayText[i] = '\0';

				int textX = 20;
				int textY = (gameState == STATE_INTRO_1) ? 60 : 30;

				printStringColorZ(chain, &font, textX + 1, textY + 1, displayText, 30, 30, 40, 1);
				printStringColorZ(chain, &font, textX, textY, displayText, 180, 180, 200, 0);

				if (introTextComplete && (frameCounter / 30) % 2 == 0) {
					const char *continuePrompt = "[START] or [X] to continue";
					int promptX = (SCREEN_WIDTH - 26 * 5) / 2;
					int promptY = 220;
					printStringColorZ(chain, &font, promptX + 1, promptY + 1, continuePrompt, 20, 20, 40, 1);
					printStringColorZ(chain, &font, promptX, promptY, continuePrompt, 100, 150, 255, 0);
				}
			} else if (gameState == STATE_DAY_INTRO) {
				char dayText[64];
				dayText[0] = 'D'; dayText[1] = 'A'; dayText[2] = 'Y'; dayText[3] = ' ';
				dayText[4] = '0' + currentDay;
				dayText[5] = '\0';

				/* Build threat text with dynamic threat level based on day */
				int threatIdx = currentDay - 1;
				if (threatIdx < 0) threatIdx = 0;
				if (threatIdx > 4) threatIdx = 4;
				const char *threatLevel = THREAT_LEVELS[threatIdx];

				char threatText[48];
				sprintf(threatText, "Threat level: %s", threatLevel);

				/* Color based on threat level */
				uint8_t threatR, threatG, threatB;
				switch (threatIdx) {
					case 0: threatR = 100; threatG = 200; threatB = 100; break;  /* Low: Green */
					case 1: threatR = 200; threatG = 200; threatB = 100; break;  /* Moderate: Yellow */
					case 2: threatR = 255; threatG = 180; threatB = 80; break;   /* High: Orange */
					case 3: threatR = 255; threatG = 100; threatB = 80; break;   /* Severe: Red-Orange */
					default: threatR = 255; threatG = 50; threatB = 50; break;   /* Critical: Red */
				}

				int dayX = (SCREEN_WIDTH - 5 * 8) / 2;
				int dayY = 90;

				printStringColorZ(chain, &font, dayX + 1, dayY + 1, dayText, 40, 20, 20, 1);
				printStringColorZ(chain, &font, dayX, dayY, dayText, 255, 220, 100, 0);

				/* Calculate text width for centering */
				int threatLen = 14;  /* "Threat level: " */
				const char *p = threatLevel;
				while (*p) { threatLen++; p++; }
				int threatX = (SCREEN_WIDTH - threatLen * 5) / 2;
				int threatY = 120;
				printStringColorZ(chain, &font, threatX + 1, threatY + 1, threatText, 20, 20, 20, 1);
				printStringColorZ(chain, &font, threatX, threatY, threatText, threatR, threatG, threatB, 0);
			} else if (gameState == STATE_ENDING || gameState == STATE_ENDING_2) {
				/* Game ending sequence (two screens) */
				const char *text = (gameState == STATE_ENDING) ? ENDING_TEXT_1 : ENDING_TEXT_2;
				int textLen = 0;
				for (const char *p = text; *p; p++) textLen++;

				if (!introTextComplete && (frameCounter % 2 == 0)) {
					introCharCount += INTRO_TEXT_SPEED;
					if (introCharCount >= textLen) {
						introCharCount = textLen;
						introTextComplete = true;
					}
				}

				char displayText[512];
				int i;
				for (i = 0; i < introCharCount && i < 511 && text[i]; i++) {
					displayText[i] = text[i];
				}
				displayText[i] = '\0';

				int textX = 20;
				int textY = 20;

				printStringColorZ(chain, &font, textX + 1, textY + 1, displayText, 30, 30, 40, 1);
				printStringColorZ(chain, &font, textX, textY, displayText, 180, 200, 180, 0);

				/* Show "Thank you" only on second ending screen */
				if (gameState == STATE_ENDING_2 && introTextComplete && (frameCounter / 30) % 2 == 0) {
					const char *thanksPrompt = "Thank you for playing";
					int promptX = (SCREEN_WIDTH - 21 * 5) / 2;
					int promptY = 220;
					printStringColorZ(chain, &font, promptX + 1, promptY + 1, thanksPrompt, 20, 30, 20, 1);
					printStringColorZ(chain, &font, promptX, promptY, thanksPrompt, 100, 200, 150, 0);
				}
			}
		} else {
			/* Normal game rendering */
			bool renderInterior = (gameState == STATE_INTERIOR) ||
				(gameState == STATE_DIALOG && currentHouseIndex >= 0) ||
				(gameState == STATE_PAUSED && prePauseState == STATE_INTERIOR) ||
				(gameState == STATE_FADE_OUT && !transitionToInterior) ||
				((gameState == STATE_BLACK || gameState == STATE_FADE_IN) && transitionToInterior);

			/* Camera handling */
			if (!renderInterior) {
				while (orbitAngle > 2048) orbitAngle -= 4096;
				while (orbitAngle < -2048) orbitAngle += 4096;

				int32_t playerWorldX = player.x >> 12;
				int32_t playerWorldY = player.y >> 12;
				int32_t playerWorldZ = player.z >> 12;

				cameraOrbit(&cam, playerWorldX, playerWorldY, playerWorldZ,
					orbitAngle, CAMERA_DISTANCE, -CAMERA_Y_OFFSET);
				cameraAddPitch(&cam, CAMERA_PITCH_OFFSET);
			} else {
				int32_t interiorCamDist = INTERIOR_CAMERA_DISTANCE;
				int32_t interiorCamY = INTERIOR_CAMERA_Y_OFFSET;
				if (currentHouseIndex == NUM_HOUSES) {
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
				cameraOrbit(&cam, 0, FLOOR_Y, 0, INTERIOR_CAMERA_ANGLE, interiorCamDist, -interiorCamY);
			}

			uint16_t t2 = TIMER_VALUE(2);
			uint16_t t3, t4;

			int topR, topG, topB, botR, botG, botB;

			if (renderInterior) {
				t3 = TIMER_VALUE(2);
				statFloorTime = 0;

				if (currentHouseIndex >= 0) {
					int16_t interiorRotation = 0;
					int32_t modelOffsetX = 0, modelOffsetZ = 0;
					int32_t doorX = 0, doorZ = 0, doorSizeX = 0, doorSizeZ = 0;
					int32_t floorHalfX = 0, floorHalfZ = 0;
					Model *interiorModel;

					if (currentHouseIndex == NUM_HOUSES) {
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
						int modelType = mapHouses[currentHouseIndex].modelType % 3;
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

					House interiorHouse;
					interiorHouse.model = *interiorModel;
					interiorHouse.x = modelOffsetX;
					interiorHouse.y = FLOOR_Y;
					interiorHouse.z = modelOffsetZ;
					interiorHouse.rotation = interiorRotation;
					drawHouse(chain, &interiorHouse, &cam);

					#if DEBUG_DRAW_COLLISION
					drawInteriorDebug(chain, &cam, doorX, doorZ, doorSizeX, doorSizeZ, floorHalfX, floorHalfZ);
					#endif
				}

				drawCharacter(chain, &player, &cam);
				if (hasFood) {
					drawCharacterItem(chain, &player, &foodBoxModel, &cam,
						CARRY_BOX_OFFSET_Y, CARRY_BOX_OFFSET_Z,
						CARRY_BOX_BOB_AMOUNT, FOOD_BOX_SCALE);
				} else if (hasMask) {
					/* Draw mask in player's hands */
					drawCharacterItem(chain, &player, &maskModel, &cam,
						CARRY_BOX_OFFSET_Y, CARRY_BOX_OFFSET_Z,
						CARRY_BOX_BOB_AMOUNT, MASK_CARRY_SCALE);
				}

				if (currentHouseIndex == NUM_HOUSES) {
					/* Day 5: Mom is gone, only food box on floor */
					if (currentDay < 5) {
						drawCharacter(chain, &mom, &cam);
					}
					if (foodBoxSpawned && !hasFood) {
						/* Day 5: food box on floor (Y=0), otherwise on table */
						int32_t boxY = (currentDay >= 5) ? 0 : FOOD_BOX_TABLE_Y;
						drawWorldItem(chain, &foodBoxModel, &cam,
							FOOD_BOX_POS_X, boxY, FOOD_BOX_POS_Z,
							0, FOOD_BOX_SCALE);
					}
#if DEBUG_CHARACTERS
					/* Draw restaurant citizens (debug mode only) */
					for (int i = 0; i < NUM_RESTAURANT_CITIZENS; i++) {
						drawCharacter(chain, &restaurantCitizens[i], &cam);
					}
#endif
#if DEBUG_ENFORCER_NEARBY
					/* Draw a static enforcer in restaurant for Y offset/scale testing */
					{
						/* Position enforcer near mom for comparison */
						int32_t debugEnfX = 100 << 12;    /* World X */
						int32_t debugEnfY = FLOOR_Y << 12; /* Ground level */
						int32_t debugEnfZ = -50 << 12;    /* World Z */
						drawEnforcer(chain,
							&enforcerBodyModel, &enforcerLegLeftModel, &enforcerLegRightModel,
							debugEnfX, debugEnfY, debugEnfZ,
							2048,       /* Facing toward player (180 degrees) */
							0, false,   /* Not walking */
							&cam);
					}
#endif
				} else if (currentHouseIndex >= 0 && currentHouseIndex < NUM_HOUSES) {
					/* Check if an adult is in this house */
					bool hasAdultInHouse = false;
					for (int i = 0; i < NUM_HIDING_ADULTS; i++) {
						if (hidingAdults[i].houseIndex == currentHouseIndex) {
							hasAdultInHouse = true;
							break;
						}
					}

					/* Draw house citizens (max 2 total, so only 1 if adult present) */
					int numCitizens = hasAdultInHouse ? 1 : NUM_CITIZENS_PER_HOUSE;
					for (int i = 0; i < numCitizens; i++) {
						drawCharacter(chain, &houseCitizens[currentHouseIndex][i], &cam);
					}
				}

				/* Draw hiding adults in current location */
				for (int i = 0; i < NUM_HIDING_ADULTS; i++) {
					if (hidingAdults[i].houseIndex == currentHouseIndex) {
						drawCharacter(chain, &hidingAdultChars[i], &cam);
						/* Draw mask on their face if they have one */
						if (hidingAdults[i].hasMask) {
							drawCharacterItem(chain, &hidingAdultChars[i], &maskModel, &cam,
								MASK_OFFSET_Y, MASK_OFFSET_Z, 0, MASK_SCALE);
						}
					}
				}

				t4 = TIMER_VALUE(2);
				statCharTime = (uint16_t)(t4 - t3);

				topR = INTERIOR_BG_TOP_R + ((BG_FLASH_TOP_R - INTERIOR_BG_TOP_R) * bgFlash) / 255;
				topG = INTERIOR_BG_TOP_G + ((BG_FLASH_TOP_G - INTERIOR_BG_TOP_G) * bgFlash) / 255;
				topB = INTERIOR_BG_TOP_B + ((BG_FLASH_TOP_B - INTERIOR_BG_TOP_B) * bgFlash) / 255;
				botR = INTERIOR_BG_BOT_R + ((BG_FLASH_BOT_R - INTERIOR_BG_BOT_R) * bgFlash) / 255;
				botG = INTERIOR_BG_BOT_G + ((BG_FLASH_BOT_G - INTERIOR_BG_BOT_G) * bgFlash) / 255;
				botB = INTERIOR_BG_BOT_B + ((BG_FLASH_BOT_B - INTERIOR_BG_BOT_B) * bgFlash) / 255;
			} else {
				/* Exterior scene */
				drawFloor(chain, &cam);
				t3 = TIMER_VALUE(2);
				statFloorTime = (uint16_t)(t3 - t2);

				drawCharacter(chain, &player, &cam);
				if (hasFood) {
					drawCharacterItem(chain, &player, &foodBoxModel, &cam,
						CARRY_BOX_OFFSET_Y, CARRY_BOX_OFFSET_Z,
						CARRY_BOX_BOB_AMOUNT, FOOD_BOX_SCALE);
				} else if (hasMask) {
					/* Draw mask in player's hands */
					drawCharacterItem(chain, &player, &maskModel, &cam,
						CARRY_BOX_OFFSET_Y, CARRY_BOX_OFFSET_Z,
						CARRY_BOX_BOB_AMOUNT, MASK_CARRY_SCALE);
				}
				for (int i = 0; i < NUM_HOUSES; i++) {
					drawHouse(chain, &houses[i], &cam);
				}
				drawHouse(chain, &restaurant, &cam);

				setupTreeBatch(&cam);
				for (int i = 0; i < NUM_MAP_TREES; i++) {
					drawTree(chain, &trees[i], &cam);
				}

				setupFenceBatch(&cam);
				for (int i = 0; i < NUM_FENCE_POSTS; i++) {
					drawFencePost(chain, &mapFencePosts[i], &cam);
				}

				/* Draw enforcers (body + animated legs) */
				for (int i = 0; i < MAX_ENFORCERS; i++) {
					if (!enforcers[i].isActive) continue;

					bool enforcerWalking = (enforcers[i].state != ENFORCER_ALERT);
					drawEnforcer(chain,
						&enforcerBodyModel, &enforcerLegLeftModel, &enforcerLegRightModel,
						enforcers[i].x,
						enforcers[i].y,
						enforcers[i].z,
						enforcers[i].facing,
						enforcers[i].walkCycle,
						enforcerWalking,
						&cam);
				}

				#if DEBUG_DRAW_COLLISION
				drawAllCollisionDebug(chain, houses, NUM_HOUSES, &cam);
				drawHouseCollisionDebug(chain, &restaurant, &cam);
				drawAllDoorTriggersDebug(chain, houses, NUM_HOUSES, &cam);
				drawDoorTriggerDebug(chain, &restaurant, &cam);
				#endif

				t4 = TIMER_VALUE(2);
				statCharTime = (uint16_t)(t4 - t3);

				topR = BG_TOP_R + ((BG_FLASH_TOP_R - BG_TOP_R) * bgFlash) / 255;
				topG = BG_TOP_G + ((BG_FLASH_TOP_G - BG_TOP_G) * bgFlash) / 255;
				topB = BG_TOP_B + ((BG_FLASH_TOP_B - BG_TOP_B) * bgFlash) / 255;
				botR = BG_BOT_R + ((BG_FLASH_BOT_R - BG_BOT_R) * bgFlash) / 255;
				botG = BG_BOT_G + ((BG_FLASH_BOT_G - BG_BOT_G) * bgFlash) / 255;
				botB = BG_BOT_B + ((BG_FLASH_BOT_B - BG_BOT_B) * bgFlash) / 255;
			}

			if (gameState == STATE_PAUSED) {
				ptr = allocatePacket(chain, ORDERING_TABLE_SIZE - 1, 3);
				ptr[0] = gp0_rgb(15, 20, 35) | gp0_rectangle(false, false, false);
				ptr[1] = gp0_xy(0, 0);
				ptr[2] = gp0_xy(SCREEN_WIDTH, SCREEN_HEIGHT);

				int32_t mapX = (prePauseState == STATE_INTERIOR) ? (entryPosX >> 12) : (player.x >> 12);
				int32_t mapZ = (prePauseState == STATE_INTERIOR) ? (entryPosZ >> 12) : (player.z >> 12);
				drawPauseMap(chain, mapX, mapZ, player.facing, frameCounter, enforcers, MAX_ENFORCERS);

				printStringColor(chain, &font, 130, 8, "PAUSED", 255, 255, 100);
				printStringColor(chain, &font, 100, 220, "press START to resume", 150, 150, 150);
			} else {
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

			/* Performance stats */
			uint16_t cpuEnd = TIMER_VALUE(2);
			uint16_t currentFrameTime = (uint16_t)(cpuEnd - frameStart);
			statFrameTime = (statFrameTime * 7 + currentFrameTime) / 8;

			int fps = (statFrameTime > 0) ? (4225000 / statFrameTime) : 60;
			if (fps > 99) fps = 99;
			int cpuPercent = ((long)statFrameTime * 100) / 70416;
			if (cpuPercent > 99) cpuPercent = 99;

			char cpuBar[12];
			int filled = (cpuPercent + 5) / 10;
			for (int i = 0; i < 10; i++) {
				cpuBar[i] = (i < filled) ? '#' : '-';
			}
			cpuBar[10] = '\0';

			#if DEBUG_UI
			int padPct = ((long)statPadTime * 100) / 70416;
			int floorPct = ((long)statFloorTime * 100) / 70416;
			int charPct = ((long)statCharTime * 100) / 70416;

			char debugText[128];
			sprintf(debugText, "FPS:%2d Tri:%3d [%s]%2d%%\nPad:%2d%% Floor:%2d%% Char:%2d%%",
				fps, statTriangles, cpuBar, cpuPercent,
				padPct, floorPct, charPct);
			printString(chain, &font, 8, 8, debugText);
			#endif

			/* Draw detection meter when enforcers spot player (exterior only) */
			if (!renderInterior && maxDetectionLevel > 0) {
				drawDetectionMeter(chain, maxDetectionLevel, DETECTION_MAX, frameCounter, &font);
			}

			/* Display delivery target when player has food */
			if (hasFood && targetHouseIndex >= 0 && targetHouseIndex < NUM_HOUSES) {
				char deliveryText[32];
				uint16_t targetAddr = mapHouses[targetHouseIndex].address;
				sprintf(deliveryText, "Deliver to: House %d", targetAddr);
				int deliveryX = 180;
				int deliveryY = 10;
				printStringColorZ(chain, &font, deliveryX + 1, deliveryY + 1, deliveryText, 20, 20, 40, 1);
				printStringColorZ(chain, &font, deliveryX, deliveryY, deliveryText, 255, 200, 100, 0);
			}

			/* Display mask delivery target when player has mask */
			if (hasMask) {
				/* Find first adult who needs a mask */
				for (int i = 0; i < NUM_HIDING_ADULTS; i++) {
					if (!hidingAdults[i].hasMask && hidingAdults[i].houseIndex < NUM_HOUSES) {
						char maskText[32];
						uint16_t houseAddr = mapHouses[hidingAdults[i].houseIndex].address;
						sprintf(maskText, "Mask for: House %d", houseAddr);
						int maskX = 185;
						int maskY = 10;
						printStringColorZ(chain, &font, maskX + 1, maskY + 1, maskText, 20, 40, 20, 1);
						printStringColorZ(chain, &font, maskX, maskY, maskText, 100, 255, 150, 0);
						break;  /* Only show first adult needing mask */
					}
				}
			}

			/* Display door prompt */
			if (gameState == STATE_EXTERIOR && triggeredDoor >= 0) {
				char addrText[20];
				int addrX;
				if (triggeredDoor == NUM_HOUSES) {
					sprintf(addrText, "Restaurant");
					addrX = 115;
				} else {
					uint16_t houseAddr = mapHouses[triggeredDoor].address;
					sprintf(addrText, "House %d", houseAddr);
					addrX = 125;
				}

				const char *doorPrompt = "press [X] to enter";
				int promptX = 110;
				int promptY = 190;

				printStringColorZ(chain, &font, addrX + 1, promptY - 9, addrText, 20, 20, 40, 1);
				printStringColorZ(chain, &font, addrX, promptY - 10, addrText, 255, 220, 100, 0);

				printStringColorZ(chain, &font, promptX + 1, promptY + 1, doorPrompt, 20, 20, 40, 1);
				printStringColorZ(chain, &font, promptX, promptY, doorPrompt, 100, 150, 255, 0);
			} else if (gameState == STATE_INTERIOR && atInteriorExit) {
				const char *doorPrompt = "press [X] to leave";
				int promptX = 110;
				int promptY = 200;
				printStringColorZ(chain, &font, promptX + 1, promptY + 1, doorPrompt, 20, 20, 40, 1);
				printStringColorZ(chain, &font, promptX, promptY, doorPrompt, 100, 150, 255, 0);
			} else if (gameState == STATE_INTERIOR && nearMomForPrompt) {
				const char *talkPrompt = "press [X] to talk";
				int promptX = 115;
				int promptY = 200;
				printStringColorZ(chain, &font, promptX + 1, promptY + 1, talkPrompt, 20, 20, 40, 1);
				printStringColorZ(chain, &font, promptX, promptY, talkPrompt, 100, 150, 255, 0);
			} else if (gameState == STATE_INTERIOR && nearFoodBox) {
				const char *pickupPrompt = "press [X] to pick up food";
				int promptX = 90;
				int promptY = 200;
				printStringColorZ(chain, &font, promptX + 1, promptY + 1, pickupPrompt, 20, 20, 40, 1);
				printStringColorZ(chain, &font, promptX, promptY, pickupPrompt, 100, 150, 255, 0);
			} else if (gameState == STATE_INTERIOR && nearHouseNPC) {
				const char *interactPrompt = "press [X] to interact";
				int promptX = 105;
				int promptY = 200;
				printStringColorZ(chain, &font, promptX + 1, promptY + 1, interactPrompt, 20, 20, 40, 1);
				printStringColorZ(chain, &font, promptX, promptY, interactPrompt, 100, 150, 255, 0);
			}

			/* Draw dialog box */
			if (gameState == STATE_DIALOG && currentDialog != NULL) {
				int boxX = 10;
				int boxY = 10;
				int boxW = SCREEN_WIDTH - 20;
				int boxH = 100;  /* Taller box for longer text */

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

				ptr = allocatePacket(chain, 1, 3);
				ptr[0] = gp0_rgb(100, 80, 150) | 0x40000000;
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

				static char dialogBuffer[256];
				int i = 0;
				const char *src = currentDialog;
				while (*src && i < dialogCharCount && i < 255) {
					dialogBuffer[i++] = *src++;
				}
				dialogBuffer[i] = '\0';

				int textX = boxX + 5;
				int textY = boxY + 5;
				printStringColor(chain, &font, textX, textY, dialogBuffer, 220, 200, 255);

				if (dialogComplete) {
					const char *continueText = "[X] continue";
					int contX = boxX + boxW - 70;
					int contY = boxY + boxH + 5;
					if ((frameCounter / 20) % 2 == 0) {
						printStringColor(chain, &font, contX, contY, continueText, 150, 200, 255);
					}
				}
			}

			/* Draw credits overlay when leaving restaurant for the first time */
			if (showingCredits) {
				const char *credit1 = "Music by Jesse Curtis";
				const char *credit2 = "Game by Ruben Tipprach";

				/* Determine which credit to show and calculate fade */
				const char *currentCredit;
				int creditX, creditY;
				int creditAlpha = 255;

				if (creditsTimer > CREDIT_HALF_DURATION) {
					/* First half: show credit 1 */
					currentCredit = credit1;
					creditX = 75;
					creditY = 105;
					int localTimer = creditsTimer - CREDIT_HALF_DURATION;
					if (localTimer > CREDIT_HALF_DURATION - 30) {
						/* Fade in over 0.5 second */
						creditAlpha = ((CREDIT_HALF_DURATION - localTimer) * 255) / 30;
					} else if (localTimer < 30) {
						/* Fade out over 0.5 second */
						creditAlpha = (localTimer * 255) / 30;
					}
				} else {
					/* Second half: show credit 2 */
					currentCredit = credit2;
					creditX = 70;
					creditY = 105;
					int localTimer = creditsTimer;
					if (localTimer > CREDIT_HALF_DURATION - 30) {
						/* Fade in over 0.5 second */
						creditAlpha = ((CREDIT_HALF_DURATION - localTimer) * 255) / 30;
					} else if (localTimer < 30) {
						/* Fade out over 0.5 second */
						creditAlpha = (localTimer * 255) / 30;
					}
				}

				/* Scale colors by alpha */
				int r1 = (200 * creditAlpha) / 255;
				int g1 = (220 * creditAlpha) / 255;
				int b1 = (255 * creditAlpha) / 255;

				/* Shadow offset colors (darker) */
				int rs = (30 * creditAlpha) / 255;
				int gs = (30 * creditAlpha) / 255;
				int bs = (40 * creditAlpha) / 255;

				/* Draw current credit with shadow */
				printStringColorZ(chain, &font, creditX + 1, creditY + 1, currentCredit, rs, gs, bs, 1);
				printStringColorZ(chain, &font, creditX, creditY, currentCredit, r1, g1, b1, 0);
			}

			/* Draw fade overlay */
			if (fadeAlpha > 0) {
				ptr = allocatePacket(chain, 0, 1);
				ptr[0] = gp0_texpage(0x00, false, false);

				int numQuads = (fadeAlpha * 4) / 255;
				if (numQuads < 1) numQuads = 1;
				if (numQuads > 4) numQuads = 4;

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
		}

		/* Set drawing area attributes */
		ptr = allocatePacket(chain, ORDERING_TABLE_SIZE - 1, 4);
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
