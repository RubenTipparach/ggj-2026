# Neighborhood Generation System

This document describes the map-based world generation system that creates the game neighborhood from a simple PNG image.

## Overview

The neighborhood is defined by a 64x64 pixel PNG image (`assets/neighborhood.png`). A Python script parses this image and generates C code that places houses, trees, fences, and streets in the game world.

## Map Scale

- **1 pixel = 64 world units**
- **Total map size:** 4096 x 4096 world units
- **Origin:** Map center (pixel 32,32) corresponds to world (0,0)

## Color Legend

The map uses exact RGB colors to define world elements:

| Color | RGB Value | Description |
|-------|-----------|-------------|
| Dark Blue | (29, 43, 83) | Out of bounds / background |
| Brown | (116, 47, 41) | Fence perimeter |
| Grey | (95, 87, 79) | Street tiles |
| Teal | (18, 83, 89) | Trees |
| Red/Magenta | (195, 0, 76) | Houses |
| Bright Green | (0, 178, 81) | Player spawn (restaurant) |

## Generated Files

The parser generates two files:

### `src/game/world_data.h`
Contains:
- Map configuration constants (`MAP_PIXELS`, `MAP_SCALE`, `MAP_WORLD_SIZE`)
- Player spawn position (`PLAYER_SPAWN_X`, `PLAYER_SPAWN_Z`)
- Data structure definitions (`HouseSpawn`, `TreeSpawn`, `FenceSegment`)
- Count macros (`NUM_MAP_HOUSES`, `NUM_MAP_TREES`, `NUM_FENCE_SEGMENTS`)
- Street tile lookup macro (`IS_STREET_PIXEL`)

### `src/game/world_data.c`
Contains:
- `mapHouses[]` - Array of house positions, rotations, and model types
- `mapTrees[]` - Array of tree positions and variants
- `mapFenceSegments[]` - Array of fence line segment endpoints
- `streetTileBitmap[]` - 512-byte bitmap for fast street tile lookup

## Data Structures

### HouseSpawn
```c
typedef struct {
    int32_t x, z;           // World position
    int16_t rotation;       // Y rotation (0, 1024, 2048, 3072 = 0°, 90°, 180°, 270°)
    uint8_t modelType;      // 0=hut1, 1=hut2, 2=hut3
} HouseSpawn;
```

House rotation is automatically determined by finding the nearest street direction. The modelType cycles through available house models.

### TreeSpawn
```c
typedef struct {
    int32_t x, z;           // World position
    uint8_t variant;        // 0=large tree, 1=small tree
} TreeSpawn;
```

Tree variants alternate between large and small.

### FenceSegment
```c
typedef struct {
    int32_t x1, z1;         // Start point
    int32_t x2, z2;         // End point
} FenceSegment;
```

The parser traces connected fence pixels and simplifies them into line segments, preserving diagonal connections.

## Running the Parser

```bash
python tools/parseNeighborhood.py assets/neighborhood.png src/game/world_data.h src/game/world_data.c
```

Output:
```
Loading assets/neighborhood.png...
  Player spawn at pixel (5, 32)
  Player spawn world position: (-1728, 0)
Finding houses...
  House 0: pixel (40, 13) -> world (512, -1216), rotation 2048, model 0
  ...
Finding trees...
  Found 19 tree positions
Tracing fence segments...
  Found 14 fence segments
Building street tile bitmap...
  Found 135 street pixels
Writing src/game/world_data.h...
Writing src/game/world_data.c...
Done!
```

## Street Tile System

Streets are stored as a compact bitmap (512 bytes for 64x64 grid). The `IS_STREET_PIXEL(px, pz)` macro provides O(1) lookup.

In `drawFloor()`, tile coordinates are converted to map pixels to determine if a tile should be rendered as street (grey) or grass (green variants).

Coordinate conversion:
```c
// Tile center to world coordinates
int32_t tileCenterX = tileX * FLOOR_TILE_SIZE + FLOOR_TILE_SIZE / 2;
int32_t tileCenterZ = tileZ * FLOOR_TILE_SIZE + FLOOR_TILE_SIZE / 2;

// World to map pixel
int mapPixelX = (tileCenterX + MAP_WORLD_SIZE / 2) / MAP_SCALE;
int mapPixelZ = (tileCenterZ + MAP_WORLD_SIZE / 2) / MAP_SCALE;
```

## Fence Rendering

Fences are rendered as two-sided quads (4 triangles per segment):
- Front face in fence color
- Back face slightly darker for depth perception
- Height defined by `FENCE_HEIGHT` constant
- Collision uses line-to-circle distance check with `FENCE_COLLISION_THICKNESS`

## Configuration

Tunable constants in `src/game/game_config.h`:

### Fence Settings
```c
#define FENCE_HEIGHT            120     // World units
#define FENCE_COLLISION_THICKNESS  30   // World units
#define FENCE_COLOR_R/G/B       100, 70, 40  // Brown wood
```

### Street Colors
```c
#define STREET_COLOR_1_R/G/B    90, 90, 95    // Primary grey
#define STREET_COLOR_2_R/G/B    100, 100, 105 // Secondary grey
```

### Tree Settings
```c
#define TREE_SCALE              3596    // 4096 = 1.0x scale
#define TREE_COLLISION_RADIUS   60      // World units
```

## Adding New Elements

To add a new element type:

1. Define a new color in `parseNeighborhood.py`
2. Add detection logic in the parser
3. Generate appropriate data structure
4. Add rendering function in `main.c`
5. Add collision checking if needed
6. Add to render loop

## Collision System

The game checks collisions with:
- Houses (AABB boxes)
- Trees (circle collision)
- Fences (line segment to circle)

All collision checks support wall sliding for smoother movement.
