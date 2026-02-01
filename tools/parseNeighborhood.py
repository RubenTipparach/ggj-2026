#!/usr/bin/env python3
"""
Parse neighborhood.png map and generate world_data.h/c for PS1 game.

Map colors (exact RGB values):
- (29, 43, 83): Dark blue - out of bounds
- (116, 47, 41): Brown - fence perimeter
- (95, 87, 79): Grey - street tiles
- (18, 83, 89): Teal/dark green - trees
- (195, 0, 76): Red/magenta - houses
- (0, 178, 81): Bright green - player spawn (restaurant)
"""

import argparse
from PIL import Image
from pathlib import Path
import math

# Exact colors from the map
COLOR_BACKGROUND = (29, 43, 83)    # Dark blue - out of bounds
COLOR_FENCE = (116, 47, 41)        # Brown - fence
COLOR_STREET = (95, 87, 79)        # Grey - street
COLOR_TREE = (18, 83, 89)          # Teal - trees
COLOR_HOUSE = (195, 0, 76)         # Red/magenta - houses
COLOR_SPAWN = (0, 178, 81)         # Bright green - player spawn

# Map configuration
MAP_PIXELS = 64
FLOOR_TILE_SIZE = 512  # Must match FLOOR_TILE_SIZE in game_config.h
TILES_PER_PIXEL = 1    # Each map pixel represents this many floor tiles
MAP_SCALE = FLOOR_TILE_SIZE * TILES_PER_PIXEL  # 1 pixel = 512 world units
MAP_WORLD_SIZE = MAP_PIXELS * MAP_SCALE  # 32768


def pixel_to_world(px, pz):
    """Convert pixel coordinates to world coordinates (centered at origin).

    Note: Z is negated because image Y increases downward but world Z North is negative.
    """
    wx = (px - MAP_PIXELS // 2) * MAP_SCALE
    wz = -((pz - MAP_PIXELS // 2) * MAP_SCALE)  # Negate Z to fix N/S mirroring
    return wx, wz


def find_connected_regions(img, target_color):
    """Find connected regions of a specific color using flood fill."""
    width, height = img.size
    visited = set()
    regions = []

    def flood_fill(start_x, start_y):
        """Return all pixels in connected region."""
        region = []
        stack = [(start_x, start_y)]
        while stack:
            x, y = stack.pop()
            if (x, y) in visited:
                continue
            if x < 0 or x >= width or y < 0 or y >= height:
                continue
            if img.getpixel((x, y)) != target_color:
                continue
            visited.add((x, y))
            region.append((x, y))
            # 4-connected neighbors
            stack.extend([(x+1, y), (x-1, y), (x, y+1), (x, y-1)])
        return region

    for y in range(height):
        for x in range(width):
            if (x, y) not in visited and img.getpixel((x, y)) == target_color:
                region = flood_fill(x, y)
                if region:
                    regions.append(region)

    return regions


def region_center(region):
    """Calculate center of a region."""
    if not region:
        return 0, 0
    sum_x = sum(p[0] for p in region)
    sum_y = sum(p[1] for p in region)
    return sum_x // len(region), sum_y // len(region)


def find_adjacent_street_direction(img, house_region):
    """Find direction to adjacent street tile for house orientation.

    Checks pixels immediately adjacent to the house region to find streets,
    then returns the rotation to face that direction.
    """
    width, height = img.size

    # Build set of house pixels for fast lookup
    house_pixels = set(house_region)

    # Direction definitions: (dx, dy, rotation)
    # rotation is the angle the house door should face (rotated 90 deg right/CW from street direction)
    directions = [
        (0, -1, 1024),   # Street to North -> door faces East (rotation 1024)
        (1, 0, 2048),    # Street to East -> door faces South (rotation 2048)
        (0, 1, 3072),    # Street to South -> door faces West (rotation 3072)
        (-1, 0, 0),      # Street to West -> door faces North (rotation 0)
    ]

    # Count adjacent street tiles in each direction
    direction_counts = {0: 0, 1024: 0, 2048: 0, 3072: 0}

    for hx, hy in house_region:
        for dx, dy, rotation in directions:
            nx, ny = hx + dx, hy + dy
            # Check if neighbor is outside house and is a street
            if (nx, ny) not in house_pixels:
                if 0 <= nx < width and 0 <= ny < height:
                    if img.getpixel((nx, ny)) == COLOR_STREET:
                        direction_counts[rotation] += 1

    # Pick direction with most adjacent street tiles
    best_rotation = 0
    best_count = 0
    for rotation, count in direction_counts.items():
        if count > best_count:
            best_count = count
            best_rotation = rotation

    # If no adjacent streets found, fall back to searching further out
    if best_count == 0:
        cx, cz = region_center(house_region)
        for dx, dy, rotation in directions:
            for dist in range(1, 20):
                nx = cx + dx * dist
                ny = cz + dy * dist
                if 0 <= nx < width and 0 <= ny < height:
                    if img.getpixel((nx, ny)) == COLOR_STREET:
                        return rotation

    return best_rotation


def is_fence_pixel(img, x, y):
    """Check if pixel at (x, y) is a fence pixel."""
    width, height = img.size
    if x < 0 or x >= width or y < 0 or y >= height:
        return False
    return img.getpixel((x, y)) == COLOR_FENCE


def get_fence_orientation(img, x, y):
    """Determine fence wall orientation based on neighboring fence pixels.

    Returns:
        0 = North-South wall (|) - spans Z axis, for E-W running fence sections
        1 = East-West wall (-) - spans X axis, for N-S running fence sections
        2 = Diagonal NE-SW (\\) - connects NE corner to SW corner
        3 = Diagonal NW-SE (/) - connects NW corner to SE corner
    """
    # Check 4 cardinal neighbors
    has_n = is_fence_pixel(img, x, y - 1)  # North (up in image = -Z in world)
    has_s = is_fence_pixel(img, x, y + 1)  # South (down in image = +Z in world)
    has_e = is_fence_pixel(img, x + 1, y)  # East (+X)
    has_w = is_fence_pixel(img, x - 1, y)  # West (-X)

    # Check 4 diagonal neighbors
    has_ne = is_fence_pixel(img, x + 1, y - 1)
    has_nw = is_fence_pixel(img, x - 1, y - 1)
    has_se = is_fence_pixel(img, x + 1, y + 1)
    has_sw = is_fence_pixel(img, x - 1, y + 1)

    ns_connection = has_n or has_s
    ew_connection = has_e or has_w

    # Check for diagonal patterns - specific directions
    nesw_diagonal = has_ne or has_sw  # Backslash pattern \ (NE to SW)
    nwse_diagonal = has_nw or has_se  # Forward slash pattern / (NW to SE)

    # Priority: cardinal directions first, then diagonals
    if ns_connection and not ew_connection and not nesw_diagonal and not nwse_diagonal:
        # Fence runs N-S only, wall spans N-S (|)
        return [0]
    elif ew_connection and not ns_connection and not nesw_diagonal and not nwse_diagonal:
        # Fence runs E-W only, wall spans E-W (-)
        return [1]
    elif ns_connection and ew_connection:
        # Corner piece with cardinal connections - need both walls
        return [0, 1]
    elif nesw_diagonal and nwse_diagonal:
        # Both diagonals present - this is a diagonal corner, use both
        return [2, 3]
    elif nesw_diagonal:
        # Only NE-SW diagonal (backslash \)
        return [2]
    elif nwse_diagonal:
        # Only NW-SE diagonal (forward slash /)
        return [3]
    elif ns_connection:
        # N-S with possible other connections
        return [0]
    elif ew_connection:
        # E-W with possible other connections
        return [1]
    else:
        # Isolated pixel - default to both cardinal directions for visibility
        return [0, 1]


def collect_fence_posts(img):
    """Collect all fence pixel positions with orientation (tile-based)."""
    width, height = img.size
    posts = []

    for y in range(height):
        for x in range(width):
            if img.getpixel((x, y)) == COLOR_FENCE:
                wx, wz = pixel_to_world(x, y)
                orientations = get_fence_orientation(img, x, y)
                # Add a post for each orientation (corners/diagonals get multiple)
                for orientation in orientations:
                    posts.append((wx, wz, orientation))

    return posts


def generate_header(houses, trees, fence_posts, street_bitmap, spawn_x, spawn_z):
    """Generate world_data.h content."""
    lines = [
        "/*",
        " * AUTO-GENERATED FILE - DO NOT EDIT",
        " * Generated by tools/parseNeighborhood.py from neighborhood.png",
        " */",
        "",
        "#pragma once",
        "",
        "#include <stdint.h>",
        "",
        "/*============================================================================",
        " * MAP CONFIGURATION",
        " *============================================================================*/",
        "",
        f"#define MAP_PIXELS          {MAP_PIXELS}",
        f"#define MAP_SCALE           {MAP_SCALE}",
        f"#define MAP_WORLD_SIZE      {MAP_WORLD_SIZE}",
        f"#define MAP_ORIGIN_X        (-MAP_WORLD_SIZE / 2)",
        f"#define MAP_ORIGIN_Z        (-MAP_WORLD_SIZE / 2)",
        "",
        "/*============================================================================",
        " * PLAYER SPAWN",
        " *============================================================================*/",
        "",
        f"#define PLAYER_SPAWN_X      {spawn_x}",
        f"#define PLAYER_SPAWN_Z      {spawn_z}",
        "",
        "/*============================================================================",
        " * HOUSES",
        " *============================================================================*/",
        "",
        f"#define NUM_MAP_HOUSES      {len(houses)}",
        "",
        "typedef struct {",
        "    int32_t x, z;           /* World position */",
        "    int16_t rotation;       /* Y rotation (0, 1024, 2048, 3072) */",
        "    uint16_t address;       /* House address from map coords (XXYY format) */",
        "    uint8_t modelType;      /* 0=hut1, 1=hut2, 2=hut3 */",
        "} HouseSpawn;",
        "",
        "extern const HouseSpawn mapHouses[NUM_MAP_HOUSES];",
        "",
        "/*============================================================================",
        " * TREES",
        " *============================================================================*/",
        "",
        f"#define NUM_MAP_TREES       {len(trees)}",
        "",
        "typedef struct {",
        "    int32_t x, z;           /* World position */",
        "    uint8_t variant;        /* 0=large tree, 1=small tree */",
        "} TreeSpawn;",
        "",
        "extern const TreeSpawn mapTrees[NUM_MAP_TREES];",
        "",
        "/*============================================================================",
        " * FENCE POSTS (tile-based)",
        " *============================================================================*/",
        "",
        f"#define NUM_FENCE_POSTS     {len(fence_posts)}",
        "",
        "/* Fence orientations: 0=N-S(|), 1=E-W(-), 2=diag NE-SW(\\\\), 3=diag NW-SE(/) */",
        "typedef struct {",
        "    int32_t x, z;           /* World position (tile center) */",
        "    uint8_t orientation;    /* Wall orientation (0-3) */",
        "} FencePost;",
        "",
        "extern const FencePost mapFencePosts[NUM_FENCE_POSTS];",
        "",
        "/*============================================================================",
        " * STREET TILES",
        " *============================================================================*/",
        "",
        "/* Bitmap for fast street tile lookup (64x64 grid = 512 bytes) */",
        "extern const uint8_t streetTileBitmap[512];",
        "",
        "/* Check if a map pixel is a street tile */",
        f"#define IS_STREET_PIXEL(px, pz) \\",
        f"    ((px) >= 0 && (px) < {MAP_PIXELS} && (pz) >= 0 && (pz) < {MAP_PIXELS} && \\",
        f"     (streetTileBitmap[((pz) * {MAP_PIXELS} + (px)) / 8] & (1 << (((pz) * {MAP_PIXELS} + (px)) % 8))))",
        "",
    ]
    return "\n".join(lines)


def generate_source(houses, trees, fence_posts, street_bitmap):
    """Generate world_data.c content."""
    lines = [
        "/*",
        " * AUTO-GENERATED FILE - DO NOT EDIT",
        " * Generated by tools/parseNeighborhood.py from neighborhood.png",
        " */",
        "",
        '#include "world_data.h"',
        "",
        "/*============================================================================",
        " * HOUSE DATA",
        " *============================================================================*/",
        "",
        "const HouseSpawn mapHouses[NUM_MAP_HOUSES] = {",
    ]

    for i, (x, z, rotation, address, model_type) in enumerate(houses):
        lines.append(f"    {{ .x = {x}, .z = {z}, .rotation = {rotation}, .address = {address}, .modelType = {model_type} }},")

    lines.extend([
        "};",
        "",
        "/*============================================================================",
        " * TREE DATA",
        " *============================================================================*/",
        "",
        "const TreeSpawn mapTrees[NUM_MAP_TREES] = {",
    ])

    for i, (x, z, variant) in enumerate(trees):
        lines.append(f"    {{ .x = {x}, .z = {z}, .variant = {variant} }},")

    lines.extend([
        "};",
        "",
        "/*============================================================================",
        " * FENCE POST DATA (tile-based)",
        " *============================================================================*/",
        "",
        "const FencePost mapFencePosts[NUM_FENCE_POSTS] = {",
    ])

    for x, z, orientation in fence_posts:
        lines.append(f"    {{ .x = {x}, .z = {z}, .orientation = {orientation} }},")

    lines.extend([
        "};",
        "",
        "/*============================================================================",
        " * STREET TILE BITMAP",
        " *============================================================================*/",
        "",
        "const uint8_t streetTileBitmap[512] = {",
    ])

    # Format bitmap as rows of 16 bytes
    for row in range(0, 512, 16):
        byte_strs = [f"0x{street_bitmap[row + i]:02x}" for i in range(min(16, 512 - row))]
        lines.append("    " + ", ".join(byte_strs) + ",")

    lines.extend([
        "};",
        "",
    ])

    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description='Parse neighborhood map and generate world data')
    parser.add_argument('input', help='Input PNG file (neighborhood.png)')
    parser.add_argument('output_header', help='Output header file (world_data.h)')
    parser.add_argument('output_source', help='Output source file (world_data.c)')
    args = parser.parse_args()

    print(f"Loading {args.input}...")
    img = Image.open(args.input).convert('RGB')

    if img.size != (MAP_PIXELS, MAP_PIXELS):
        print(f"Warning: Expected {MAP_PIXELS}x{MAP_PIXELS}, got {img.size}")

    # Find player spawn (bright green)
    spawn_px, spawn_pz = MAP_PIXELS // 2, MAP_PIXELS // 2  # Default center
    for y in range(img.height):
        for x in range(img.width):
            if img.getpixel((x, y)) == COLOR_SPAWN:
                spawn_px, spawn_pz = x, y
                print(f"  Player spawn at pixel ({x}, {y})")
                break
    spawn_x, spawn_z = pixel_to_world(spawn_px, spawn_pz)
    print(f"  Player spawn world position: ({spawn_x}, {spawn_z})")

    # Find houses (red regions)
    print("Finding houses...")
    house_regions = find_connected_regions(img, COLOR_HOUSE)
    houses = []

    for i, region in enumerate(house_regions):
        cx, cz = region_center(region)
        wx, wz = pixel_to_world(cx, cz)
        rotation = find_adjacent_street_direction(img, region)
        # Add 180° rotation to all houses (door direction correction)
        rotation = (rotation + 2048) % 4096
        model_type = i % 3  # Cycle through house models
        # Generate house address from pixel coordinates (XXYY format)
        address = cx * 100 + cz
        print(f"  House {i}: pixel ({cx}, {cz}) -> world ({wx}, {wz}), rotation {rotation}, address {address}, model {model_type}")

        houses.append((wx, wz, rotation, address, model_type))

    # Find trees (dark green pixels)
    print("Finding trees...")
    trees = []
    for y in range(img.height):
        for x in range(img.width):
            if img.getpixel((x, y)) == COLOR_TREE:
                wx, wz = pixel_to_world(x, y)
                variant = len(trees) % 2  # Alternate between large and small
                trees.append((wx, wz, variant))
    print(f"  Found {len(trees)} tree positions")

    # Collect fence posts (tile-based)
    print("Collecting fence posts...")
    fence_posts = collect_fence_posts(img)
    print(f"  Found {len(fence_posts)} fence posts")

    # Build street tile bitmap
    print("Building street tile bitmap...")
    street_bitmap = bytearray(512)  # 64*64/8 = 512 bytes
    street_count = 0
    for y in range(img.height):
        for x in range(img.width):
            if img.getpixel((x, y)) == COLOR_STREET:
                bit_index = y * MAP_PIXELS + x
                byte_index = bit_index // 8
                bit_offset = bit_index % 8
                street_bitmap[byte_index] |= (1 << bit_offset)
                street_count += 1
    print(f"  Found {street_count} street pixels")

    # Generate output files
    print(f"Writing {args.output_header}...")
    header_content = generate_header(houses, trees, fence_posts, street_bitmap, spawn_x, spawn_z)
    Path(args.output_header).parent.mkdir(parents=True, exist_ok=True)
    with open(args.output_header, 'w') as f:
        f.write(header_content)

    print(f"Writing {args.output_source}...")
    source_content = generate_source(houses, trees, fence_posts, street_bitmap)
    Path(args.output_source).parent.mkdir(parents=True, exist_ok=True)
    with open(args.output_source, 'w') as f:
        f.write(source_content)

    print("Done!")
    print(f"  Houses: {len(houses)}")
    print(f"  Trees: {len(trees)}")
    print(f"  Fence posts: {len(fence_posts)}")
    print(f"  Street pixels: {street_count}")


if __name__ == '__main__':
    main()
