/*
* Game Configuration - Tunable gameplay constants
*
* This file contains all gameplay-related constants that designers
* and programmers might want to tweak. Engine constants are in their
* respective engine modules.
*/

#pragma once

/*============================================================================
* CHARACTER MOVEMENT
*============================================================================*/

/* Movement speed (world units per frame) */
#define PLAYER_MOVE_SPEED       20000

/* Outdoor speed multiplier (256 = 1.0x, 512 = 2.0x, etc.) */
#define OUTDOOR_SPEED_MULT      1024

/* Turn/rotation speed (angle units per frame, 4096 = 360 degrees) */
#define PLAYER_TURN_SPEED       128

/* Starting rotation (0=North, 1024=East, 2048=South, 3072=West) */
#define PLAYER_START_ROTATION   1024

/* Player spawn offset from restaurant (to position at door)
* With RESTAURANT_ROTATION=0 (North), door is at +X side */
#define PLAYER_SPAWN_OFFSET_X   700     /* Just outside door (door at X=500) */
#define PLAYER_SPAWN_OFFSET_Z   170     /* Match door Z offset */

/* Player Y offset (vertical position adjustment, positive = down toward floor) */
#define PLAYER_Y_OFFSET         20

/*============================================================================
* CHARACTER ANIMATION
*============================================================================*/

/* Walk cycle animation speed (higher = faster leg/arm movement) */
#define WALK_CYCLE_SPEED            400     /* Outdoor walk cycle */
#define WALK_CYCLE_SPEED_INTERIOR   300     /* Indoor walk cycle (slower) */

/* Arm swing amplitude (angle units, max rotation from neutral) */
#define ARM_SWING_ANGLE         200

/* Leg swing amplitude (angle units, max rotation from neutral) */
#define LEG_SWING_ANGLE         250

/* Speed at which limbs return to neutral when stopping */
#define LIMB_RETURN_SPEED       20

/* Body squash amount during walk (0-4096, where 4096 = 100% squash) */
#define BODY_SQUASH_AMOUNT      250

/* Head bob during walk (angle units, uses double frequency like squash) */
#define WALK_HEAD_BOB           30

/* Idle breathing animation */
#define IDLE_BREATH_SPEED       30      /* How fast the breathing cycle runs */
#define IDLE_BREATH_AMOUNT      100      /* Body squash amount for breathing */
#define IDLE_HEAD_BOB           20       /* Subtle head movement amplitude */

/* Carrying animation (when holding food box) */
#define CARRY_ARM_ANGLE         600    /* Arms rotated forward/up (negative = forward) */
#define CARRY_ARM_BOB_AMOUNT    50      /* Arm bob amplitude when carrying */
#define CARRY_BOX_OFFSET_Y      -20     /* Box Y offset from body center */
#define CARRY_BOX_OFFSET_Z      40      /* Box Z offset (forward from body) */
#define CARRY_BOX_BOB_AMOUNT    4       /* Box bob amplitude during walk */

/*============================================================================
* CAMERA
*============================================================================*/

/* Field of view - focal length for GTE projection
* Higher = narrower FOV (more zoomed in)
* ~120 = ~106° FOV, ~160 = ~90° FOV, ~208 = ~75° FOV */
#define CAMERA_FOCAL_LENGTH     208

/* Base distance from camera to character (lower = more zoomed in) */
#define CAMERA_DISTANCE         350

/* Camera follow smoothing (higher = slower/smoother, 1 = instant) */
#define CAMERA_FOLLOW_DIVISOR   16

/* Camera Y offset (vertical position relative to character, positive = above) */
#define CAMERA_Y_OFFSET        100
/* Camera pitch offset (angle units, negative = look down)
* 4096 = 360°, so 341 ≈ 30°, 256 ≈ 22.5°, 512 ≈ 45° */
#define CAMERA_PITCH_OFFSET    -200

/*============================================================================
* INPUT
*============================================================================*/

/* Analog stick deadzone (0-127 range) */
#define ANALOG_DEADZONE         20

/* Rotation threshold for "face direction" turns (angle units)
* When within this threshold, stop turning to avoid jitter */
#define ROTATION_THRESHOLD      64

/*============================================================================
* WORLD OBJECTS
*============================================================================*/

/* House scale multiplier (2048 = 0.5x, 6144 = 1.5x, 4096 = 1.0x, 8192 = 2.0x, etc.) */
#define HOUSE_SCALE  3596

/* Default collision box half-size for houses (local units, before HOUSE_SCALE) */
#define HOUSE_COLLISION_SIZE    500

/* Player collision radius (world units) */
#define PLAYER_COLLISION_RADIUS 40

/* Enforcer collision radius (world units) */
#define ENFORCER_COLLISION_RADIUS 40

/* Food box scale (4096 = 1.0x) */
#define FOOD_BOX_SCALE          4096    /* 8192 = 2.0x */

/* Food box Y position when on table (negative = up from floor) */
#define FOOD_BOX_TABLE_Y        40

/* Mask rendering when worn by adults (attached to head) */
#define MASK_OFFSET_Y           -50     /* Y offset from body center (negative = up toward head) */
#define MASK_OFFSET_Z           20      /* Z offset (forward from face) */
#define MASK_SCALE              4096    /* Scale (4096 = 1.0x) - for masks worn on adults' faces */
#define MASK_CARRY_SCALE        4096    /* Scale when carried by player (1.0x) */

/* Interaction radius for talking to NPCs / picking up items (world units) */
#define INTERACT_RADIUS         150

/* Mom NPC position in restaurant (world units, within floor bounds) */
/* Floor bounds: X = -490 to +490, Z = -50 to +50 */
#define MOM_POS_X               -300
#define MOM_POS_Z               0

/* Food box spawn position in restaurant (within floor bounds) */
#define FOOD_BOX_POS_X          -100
#define FOOD_BOX_POS_Z          0

/* Number of hiding adults in the game (one per delivery per day) */
#define NUM_HIDING_ADULTS       3

/* Number of citizens in restaurant */
#define NUM_RESTAURANT_CITIZENS 2

/* Hiding adult position in restaurant (for DEBUG_CHARACTERS mode) */
#define HIDING_ADULT_POS_X      200
#define HIDING_ADULT_POS_Z      0
#define HIDING_ADULT_POS_Y      0        /* Y offset (negative = higher) */

/* Citizen positions in restaurant */
#define CITIZEN_0_POS_X         100
#define CITIZEN_0_POS_Z         0
#define CITIZEN_0_POS_Y         -10       /* Y offset for citizen 1 model */
#define CITIZEN_1_POS_X         300
#define CITIZEN_1_POS_Z         0
#define CITIZEN_1_POS_Y         -10       /* Y offset for citizen 2 model */

/* Hiding adult position in house interiors (world units) */
#define HOUSE_ADULT_POS_X       -200
#define HOUSE_ADULT_POS_Z       0
#define HOUSE_ADULT_POS_Y       0        /* Y offset */

/* House citizen positions (2 citizens per house) - spread far apart */
#define NUM_CITIZENS_PER_HOUSE  2
#define HOUSE_CITIZEN_0_POS_X   100      /* Right side of room */
#define HOUSE_CITIZEN_0_POS_Z   0
#define HOUSE_CITIZEN_0_POS_Y   30       /* Y offset for citizen 1 model */
#define HOUSE_CITIZEN_1_POS_X   -300     /* Left side of room */
#define HOUSE_CITIZEN_1_POS_Z   0
#define HOUSE_CITIZEN_1_POS_Y   -10      /* Y offset for citizen 2 model */

/* Door trigger zones per house type (local units, before HOUSE_SCALE) */

/* House 1 exterior door trigger */
#define HOUSE1_DOOR_SIZE_X      100      /* Half-width of door trigger */
#define HOUSE1_DOOR_SIZE_Z      100      /* Half-depth of door trigger */
#define HOUSE1_DOOR_OFFSET_X    500      /* X offset from house center */
#define HOUSE1_DOOR_OFFSET_Z    0        /* Z offset from house center */

/* House 1 interior settings */
#define HOUSE1_INT_ROTATION     2048     /* Interior model rotation (1024 = 90°) */
#define HOUSE1_INT_CAM_DIST     900      /* Camera distance from center */
#define HOUSE1_INT_CAM_Y        200      /* Camera Y offset */
#define HOUSE1_INT_MODEL_X      -90      /* Model X offset */
#define HOUSE1_INT_MODEL_Z      -60      /* Model Z offset */
#define HOUSE1_INT_DOOR_X       450      /* Interior door X offset */
#define HOUSE1_INT_DOOR_Z       0        /* Interior door Z offset */
#define HOUSE1_INT_DOOR_SIZE_X  100      /* Half-width of exit trigger */
#define HOUSE1_INT_DOOR_SIZE_Z  100      /* Half-depth of exit trigger */

/* House 2 exterior door trigger */
#define HOUSE2_DOOR_SIZE_X      100
#define HOUSE2_DOOR_SIZE_Z      100
#define HOUSE2_DOOR_OFFSET_X    500
#define HOUSE2_DOOR_OFFSET_Z    -100

/* House 2 interior settings */
#define HOUSE2_INT_ROTATION     2048     /* Interior model rotation (1024 = 90°) */
#define HOUSE2_INT_CAM_DIST     900      /* Camera distance from center */
#define HOUSE2_INT_CAM_Y        200      /* Camera Y offset */
#define HOUSE2_INT_MODEL_X      -90      /* Model X offset */
#define HOUSE2_INT_MODEL_Z      -180      /* Model Z offset */
#define HOUSE2_INT_DOOR_X       450      /* Interior door X offset */
#define HOUSE2_INT_DOOR_Z       0        /* Interior door Z offset */
#define HOUSE2_INT_DOOR_SIZE_X  100      /* Half-width of exit trigger */
#define HOUSE2_INT_DOOR_SIZE_Z  100      /* Half-depth of exit trigger */

/* House 3 exterior door trigger */
#define HOUSE3_DOOR_SIZE_X      100
#define HOUSE3_DOOR_SIZE_Z      100
#define HOUSE3_DOOR_OFFSET_X    500
#define HOUSE3_DOOR_OFFSET_Z    0

/* House 3 interior settings */
#define HOUSE3_INT_ROTATION     2048     /* Interior model rotation (1024 = 90°) */
#define HOUSE3_INT_CAM_DIST     900      /* Camera distance from center */
#define HOUSE3_INT_CAM_Y        200      /* Camera Y offset */
#define HOUSE3_INT_MODEL_X      -90      /* Model X offset */
#define HOUSE3_INT_MODEL_Z      -180      /* Model Z offset */
#define HOUSE3_INT_DOOR_X       450      /* Interior door X offset */
#define HOUSE3_INT_DOOR_Z       0        /* Interior door Z offset */
#define HOUSE3_INT_DOOR_SIZE_X  100      /* Half-width of exit trigger */
#define HOUSE3_INT_DOOR_SIZE_Z  100      /* Half-depth of exit trigger */

/*============================================================================
* RESTAURANT (at player spawn location)
*============================================================================*/

/* Restaurant scale multiplier (4096 = 1.0x) */
#define RESTAURANT_SCALE        3596

/* Restaurant collision box half-size (local units, before RESTAURANT_SCALE) */
#define RESTAURANT_COLLISION_SIZE_X  500    /* Half-width of collision box */
#define RESTAURANT_COLLISION_SIZE_Z  800    /* Half-depth of collision box */

/* Restaurant exterior door trigger */
#define RESTAURANT_DOOR_SIZE_X      150      /* Half-width of door trigger */
#define RESTAURANT_DOOR_SIZE_Z      150      /* Half-depth of door trigger */
#define RESTAURANT_DOOR_OFFSET_X    500      /* X offset from center */
#define RESTAURANT_DOOR_OFFSET_Z    170        /* Z offset from center */

/* Restaurant exterior rotation (facing direction) */
#define RESTAURANT_ROTATION         0        /* 0 = North (rotated 90° left from East) */

/* Restaurant interior settings */
#define RESTAURANT_INT_ROTATION     2048     /* Interior model rotation (1024 = 90°) */
#define RESTAURANT_INT_CAM_DIST     900      /* Camera distance from center */
#define RESTAURANT_INT_CAM_Y        200      /* Camera Y offset */
#define RESTAURANT_INT_MODEL_X      50      /* Model X offset */
#define RESTAURANT_INT_MODEL_Z      -120      /* Model Z offset */
#define RESTAURANT_INT_DOOR_X       450      /* Interior door X offset */
#define RESTAURANT_INT_DOOR_Z       0        /* Interior door Z offset */
#define RESTAURANT_INT_DOOR_SIZE_X  100      /* Half-width of exit trigger */
#define RESTAURANT_INT_DOOR_SIZE_Z  100      /* Half-depth of exit trigger */
#define RESTAURANT_INT_FLOOR_HALF_X 490      /* Half-width of walkable area */
#define RESTAURANT_INT_FLOOR_HALF_Z 50       /* Half-depth of walkable area */

/*============================================================================
* FLOOR / TERRAIN
*============================================================================*/

/* Size of each floor tile in world units */
#define FLOOR_TILE_SIZE  256

/* Number of tiles in each direction from center */
#define FLOOR_GRID_SIZE  8

/* Y position of floor (below character) */
#define FLOOR_Y          80

/* Floor grass colors (hex: 0c5c67, 12916b, 0b2458) */
#define GRASS_COLOR_1_R  12
#define GRASS_COLOR_1_G  92
#define GRASS_COLOR_1_B  103

#define GRASS_COLOR_2_R  18
#define GRASS_COLOR_2_G  145
#define GRASS_COLOR_2_B  107

#define GRASS_COLOR_3_R  11
#define GRASS_COLOR_3_G  36
#define GRASS_COLOR_3_B  88

/*============================================================================
* INTERIOR SCENES
*============================================================================*/

/* Interior camera settings */
#define INTERIOR_CAMERA_ANGLE       0       /* Fixed Y rotation (0 = facing +Z) */
#define INTERIOR_CAMERA_Y_OFFSET    100     /* Height above floor */
#define INTERIOR_CAMERA_DISTANCE    300     /* Distance from room center */

/* Interior floor bounds (half-size, player movement area) */
#define INTERIOR_FLOOR_HALF_X       390     /* Half-width of walkable area */
#define INTERIOR_FLOOR_HALF_Z       50     /* Half-depth of walkable area */

/* Interior background colors */
#define INTERIOR_BG_TOP_R           40
#define INTERIOR_BG_TOP_G           30
#define INTERIOR_BG_TOP_B           50
#define INTERIOR_BG_BOT_R           20
#define INTERIOR_BG_BOT_G           15
#define INTERIOR_BG_BOT_B           25

/*============================================================================
* SCENE TRANSITIONS
*============================================================================*/

/* Fade speed (alpha change per frame, 0-255) */
#define FADE_SPEED                  8

/*============================================================================
* VISUAL EFFECTS
*============================================================================*/

/* Background flash fade speed (per frame) */
#define BG_FLASH_FADE_SPEED     12

/* Background gradient colors (normal state) */
#define BG_TOP_R    60
#define BG_TOP_G    20
#define BG_TOP_B    90
#define BG_BOT_R    15
#define BG_BOT_G    5
#define BG_BOT_B    35

/* Background gradient colors (flash state) */
#define BG_FLASH_TOP_R  255
#define BG_FLASH_TOP_G  220
#define BG_FLASH_TOP_B  80
#define BG_FLASH_BOT_R  180
#define BG_FLASH_BOT_G  100
#define BG_FLASH_BOT_B  40

/* Distance fog settings */
#define FOG_NEAR_DISTANCE   1000     /* Distance where fog starts (world units) */
#define FOG_FAR_DISTANCE    2400    /* Distance where fog is fully opaque */
#define FOG_COLOR_R         BG_TOP_R  /* Fog blends to background color */
#define FOG_COLOR_G         BG_TOP_G
#define FOG_COLOR_B         BG_TOP_B

/*============================================================================
* AUDIO
*============================================================================*/

/* Sound effect playback sample rate */
#define SFX_SAMPLE_RATE         22050

/* Sound effect volume (0-0x3FFF) */
#define SFX_VOLUME              0x3FFF

/* Master volume (0-0x3FFF) */
#define MASTER_VOLUME           0x3FFF

/*============================================================================
* FENCE
*============================================================================*/

/* Fence dimensions (world units) */
#define FENCE_HEIGHT            120
#define FENCE_COLLISION_THICKNESS  30

/* Fence colors (brown wood) */
#define FENCE_COLOR_R           100
#define FENCE_COLOR_G           70
#define FENCE_COLOR_B           40

/*============================================================================
* STREET TILES
*============================================================================*/

/* Street tile colors (grey variations for visual interest) */
#define STREET_COLOR_1_R        90
#define STREET_COLOR_1_G        90
#define STREET_COLOR_1_B        95

#define STREET_COLOR_2_R        100
#define STREET_COLOR_2_G        100
#define STREET_COLOR_2_B        105

/*============================================================================
* TREES
*============================================================================*/

/* Tree scale multiplier (4096 = 1.0x) */
#define TREE_SCALE              3596

/* Tree Y offset (negative = up, moves tree above/below floor level) */
#define TREE_Y_OFFSET           0

/* Tree collision radius (world units) */
#define TREE_COLLISION_RADIUS   60

/*============================================================================
* DISTANCE CULLING
*============================================================================*/

/* Maximum distance from camera before objects are culled (world units) */
#define CULL_DISTANCE_HOUSE     3000
#define CULL_DISTANCE_TREE      3000
#define CULL_DISTANCE_FENCE     2000
#define CULL_DISTANCE_DEBUG     3000

/*============================================================================
* DEBUG
*============================================================================*/

/* Draw collision box wireframes (1 = enabled, 0 = disabled) */
#define DEBUG_DRAW_COLLISION    0

/* Show debug UI text (FPS, CPU%, timing stats) (1 = enabled, 0 = disabled) */
#define DEBUG_UI                0

/* Spawn all hiding adults in the restaurant for testing (1 = enabled, 0 = disabled) */
#define DEBUG_CHARACTERS        0

/* Spawn an enforcer near the restaurant for testing (1 = enabled, 0 = disabled) */
#define DEBUG_ENFORCER_NEARBY   0

/* Show enforcers on pause map (1 = enabled, 0 = disabled) */
#define DEBUG_VISUAL_AGENTS     0

/* Allow SELECT button to skip to next day (1 = enabled, 0 = disabled) */
#define DEBUG_SKIP_DAY          1

/*============================================================================
* ENFORCER SYSTEM
*============================================================================*/

/* Maximum enforcers (Day 5 = 5 enforcers) */
#define MAX_ENFORCERS           5

/* Maximum game days */
#define MAX_DAYS                5

/* Patrol area size (4x4 tiles = 2048 world units) */
#define PATROL_HALF_SIZE        1024

/* Enforcer movement speeds (compare: PLAYER_MOVE_SPEED = 30000) */
#define ENFORCER_PATROL_SPEED   15000   /* Slow patrol */
#define ENFORCER_CHASE_SPEED    45000   /* 1.5x player speed */

/* Detection system */
#define DETECTION_RANGE         1500    /* World units - can see player within this */
#define DETECTION_CONE          682     /* ~60 degree half-angle in angle units */
#define DETECTION_RATE          128     /* Meter fill per frame */
#define DETECTION_MAX           4096    /* Meter full = chase begins */
#define DETECTION_DECAY         64      /* Meter decay when player not visible */
#define CHASE_TIMEOUT           (60 * 3 * 256)  /* 3 seconds before giving up chase */

/* Patrol timing */
#define WAYPOINT_PAUSE_TIME     (60 * 256)  /* 1 second pause at corners */

/* Catch radius (when enforcer catches player) */
#define CATCH_RADIUS            80

/* Enforcer spawn Y position (same as floor) */
#define ENFORCER_SPAWN_Y        FLOOR_Y

/* Minimum distance from restaurant for enforcer patrol centers (10 tiles) */
#define ENFORCER_MIN_RESTAURANT_DIST   (FLOOR_TILE_SIZE * 10)  /* 2560 world units */

/* Enforcer visual scale and positioning */
#define ENFORCER_SCALE          5096    /* 1.0x scale (same as other characters) */
#define ENFORCER_Y_OFFSET       -100    /* Y offset to position feet on ground */
#define ENFORCER_LEG_OFFSET_X   8       /* Leg X offset from body center (hip width) */
#define ENFORCER_LEG_OFFSET_Y   0       /* Leg Y offset from body (hip height) */

/* Threat level display strings */
static const char *THREAT_LEVELS[] = {"Low", "Moderate", "High", "Severe", "Critical"};
