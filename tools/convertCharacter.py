#!/usr/bin/env python3
"""
Convert OBJ model with multiple objects into separate PS1 binary files for character parts.

This extracts specific named objects from an OBJ file and converts each to a separate
binary file for use as character body parts with independent transforms.

Output format per part (same as convertModel.py):
  Header (8 bytes):
    uint16_t num_vertices
    uint16_t num_uvs
    uint16_t num_faces
    uint16_t reserved

  Vertices (num_vertices * 8 bytes each, matching GTEVector16):
    int16_t x, y, z, padding

  UVs (num_uvs * 2 bytes each):
    uint8_t u, v

  Faces (num_faces * 18 bytes each):
    int16_t v0, v1, v2, v3  (v3 = -1 for triangles)
    int16_t uv0, uv1, uv2, uv3
    int16_t normal_index
"""

import argparse
import re
import struct
import sys
from pathlib import Path


def find_matching_objects(objects, pattern_name):
    """Find object names that match the given pattern.

    Matches exact name or name with .NNN suffix (e.g., 'body' matches 'body' or 'body.002').
    """
    # Escape special regex chars in the name, then allow optional .NNN suffix
    escaped = re.escape(pattern_name)
    regex = re.compile(f'^{escaped}(\\.\\d+)?$')

    matches = [name for name in objects.keys() if regex.match(name)]
    return matches


def parse_obj_by_object(filepath):
    """Parse OBJ file and return objects with their vertices, uvs, faces, and colors."""
    objects = {}
    current_object = None

    # Global vertex and UV lists (OBJ uses global indices)
    all_vertices = []
    all_uvs = []
    all_colors = []  # Vertex colors (r, g, b) for each vertex

    with open(filepath, 'r') as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('#'):
                continue

            parts = line.split()
            if not parts:
                continue

            cmd = parts[0]

            if cmd == 'o':
                # New object
                current_object = parts[1] if len(parts) > 1 else 'unnamed'
                if current_object not in objects:
                    objects[current_object] = {
                        'faces': [],
                        'vertex_indices': set(),
                        'uv_indices': set()
                    }

            elif cmd == 'v':
                # Vertex position (may include color)
                x, y, z = float(parts[1]), float(parts[2]), float(parts[3])
                all_vertices.append((x, y, z))

                # Check for vertex color (extended OBJ format: v x y z r g b)
                if len(parts) >= 7:
                    r, g, b = float(parts[4]), float(parts[5]), float(parts[6])
                    all_colors.append((r, g, b))
                else:
                    all_colors.append((1.0, 1.0, 1.0))  # Default white

            elif cmd == 'vt':
                # Texture coordinate
                u, v = float(parts[1]), float(parts[2])
                all_uvs.append((u, v))

            elif cmd == 'f' and current_object:
                # Face - belongs to current object
                face_verts = []
                face_uvs = []

                for i in range(1, len(parts)):
                    indices = parts[i].split('/')
                    v_idx = int(indices[0]) - 1  # Convert to 0-based

                    if len(indices) >= 2 and indices[1]:
                        uv_idx = int(indices[1]) - 1
                    else:
                        uv_idx = 0

                    face_verts.append(v_idx)
                    face_uvs.append(uv_idx)
                    objects[current_object]['vertex_indices'].add(v_idx)
                    objects[current_object]['uv_indices'].add(uv_idx)

                # Store face with global indices
                objects[current_object]['faces'].append({
                    'verts': face_verts,
                    'uvs': face_uvs
                })

    return objects, all_vertices, all_uvs, all_colors


def extract_object(objects, all_vertices, all_uvs, all_colors, object_names):
    """Extract vertices, uvs, colors, and faces for specified object(s), remapping indices.

    Object names support regex matching - 'body' will match 'body' or 'body.002'.
    """
    vertices = []
    uvs = []
    colors = []
    faces = []

    # Collect all vertex and UV indices used by the objects
    vertex_indices = set()
    uv_indices = set()

    # Find all matching object names using regex
    matched_names = []
    for pattern in object_names:
        matches = find_matching_objects(objects, pattern)
        if matches:
            matched_names.extend(matches)
        else:
            # Try exact match as fallback
            if pattern in objects:
                matched_names.append(pattern)

    for name in matched_names:
        vertex_indices.update(objects[name]['vertex_indices'])
        uv_indices.update(objects[name]['uv_indices'])

    # Create mapping from global to local indices
    vertex_map = {}
    for i, global_idx in enumerate(sorted(vertex_indices)):
        vertex_map[global_idx] = i
        vertices.append(all_vertices[global_idx])
        colors.append(all_colors[global_idx] if global_idx < len(all_colors) else (1.0, 1.0, 1.0))

    uv_map = {}
    for i, global_idx in enumerate(sorted(uv_indices)):
        uv_map[global_idx] = i
        if global_idx < len(all_uvs):
            uvs.append(all_uvs[global_idx])
        else:
            uvs.append((0.0, 0.0))

    # Remap face indices
    for name in matched_names:
        for face in objects[name]['faces']:
            remapped_verts = [vertex_map[v] for v in face['verts']]
            remapped_uvs = [uv_map.get(uv, 0) for uv in face['uvs']]
            faces.append({
                'verts': remapped_verts,
                'uvs': remapped_uvs
            })

    return vertices, uvs, colors, faces


def get_pivot_object_vertices(objects, all_vertices, object_names):
    """Get vertices only from the first object for pivot calculation."""
    if not object_names:
        return []

    # Use first object pattern and find matching name
    pivot_pattern = object_names[0]
    matches = find_matching_objects(objects, pivot_pattern)
    if not matches:
        # Try exact match as fallback
        if pivot_pattern in objects:
            matches = [pivot_pattern]
        else:
            return []

    pivot_name = matches[0]  # Use first match
    pivot_vertices = []
    for global_idx in objects[pivot_name]['vertex_indices']:
        pivot_vertices.append(all_vertices[global_idx])

    return pivot_vertices


def calculate_center(vertices):
    """Calculate the center point of vertices."""
    if not vertices:
        return (0, 0, 0)

    sum_x = sum(v[0] for v in vertices)
    sum_y = sum(v[1] for v in vertices)
    sum_z = sum(v[2] for v in vertices)
    n = len(vertices)

    return (sum_x / n, sum_y / n, sum_z / n)


def convert_to_binary(vertices, uvs, colors, faces, scale=28.0, tex_size=64, center=None, use_vertex_colors=False):
    """Convert parsed OBJ data to binary format."""
    data = bytearray()

    # Calculate center if not provided
    if center is None:
        center = calculate_center(vertices)

    # Header
    data.extend(struct.pack('<HHHH',
        len(vertices),
        len(uvs),
        len(faces),
        0  # reserved
    ))

    # Vertices (scaled and converted to int16, centered around origin)
    # Also store vertex colors
    for i, (x, y, z) in enumerate(vertices):
        # Center the model
        x -= center[0]
        y -= center[1]
        z -= center[2]

        # Scale and convert to 16-bit integers
        # PS1 coordinate system: X=right, Y=down (negate for up), Z=into screen
        vx = int(x * scale)
        vy = int(-y * scale)  # Negate Y (screen Y increases downward)
        vz = int(z * scale)

        # Clamp to int16 range
        vx = max(-32768, min(32767, vx))
        vy = max(-32768, min(32767, vy))
        vz = max(-32768, min(32767, vz))

        # Get vertex color (0-255 range)
        r, g, b = colors[i] if i < len(colors) else (1.0, 1.0, 1.0)
        cr = int(r * 255)
        cg = int(g * 255)
        cb = int(b * 255)

        # Pack as 8 bytes: x, y, z, color (RGB packed into 16 bits isn't enough, use padding)
        # Actually pack as: x(2), y(2), z(2), pad(2) for vertex
        # Then color as separate array after vertices
        data.extend(struct.pack('<hhhh', vx, vy, vz, 0))

    # Vertex colors (3 bytes per vertex: R, G, B)
    for i in range(len(vertices)):
        r, g, b = colors[i] if i < len(colors) else (1.0, 1.0, 1.0)
        cr = max(0, min(255, int(r * 255)))
        cg = max(0, min(255, int(g * 255)))
        cb = max(0, min(255, int(b * 255)))
        data.extend(struct.pack('<BBB', cr, cg, cb))

    # Pad to 4-byte alignment
    while len(data) % 4 != 0:
        data.append(0)

    # UVs - if no UVs, generate from vertex colors
    if not uvs or len(uvs) == 0:
        uvs = [(0.5, 0.5)]  # Default UV

    for u, v in uvs:
        pu = int(u * tex_size) % 256
        pv = int((1.0 - v) * tex_size) % 256
        data.extend(struct.pack('<BB', pu, pv))

    # Pad to 4-byte alignment
    while len(data) % 4 != 0:
        data.append(0)

    # Faces - triangulate and add to data
    triangulated_faces = []
    for face in faces:
        verts = face['verts']
        face_uvs = face['uvs']

        # Ensure we have enough UVs
        while len(face_uvs) < len(verts):
            face_uvs.append(0)

        # Triangulate (with reversed winding for PS1 backface culling)
        if len(verts) == 3:
            triangulated_faces.append({
                'verts': (verts[0], verts[2], verts[1], -1),
                'uvs': (face_uvs[0], face_uvs[2], face_uvs[1], -1)
            })
        elif len(verts) == 4:
            triangulated_faces.append({
                'verts': (verts[0], verts[2], verts[1], -1),
                'uvs': (face_uvs[0], face_uvs[2], face_uvs[1], -1)
            })
            triangulated_faces.append({
                'verts': (verts[0], verts[3], verts[2], -1),
                'uvs': (face_uvs[0], face_uvs[3], face_uvs[2], -1)
            })
        elif len(verts) > 4:
            for i in range(1, len(verts) - 1):
                triangulated_faces.append({
                    'verts': (verts[0], verts[i+1], verts[i], -1),
                    'uvs': (face_uvs[0], face_uvs[i+1], face_uvs[i], -1)
                })

    # Update header with correct face count
    struct.pack_into('<H', data, 4, len(triangulated_faces))

    for face in triangulated_faces:
        v0, v1, v2, v3 = face['verts']
        uv0, uv1, uv2, uv3 = face['uvs']

        data.extend(struct.pack('<hhhh', v0, v1, v2, v3))
        data.extend(struct.pack('<hhhh', uv0, uv1, uv2, uv3))
        data.extend(struct.pack('<h', 0))  # normal index

    # Pad to 4-byte alignment
    while len(data) % 4 != 0:
        data.append(0)

    return bytes(data), colors


def calculate_pivot_from_max_y(vertices):
    """Calculate pivot point at the top of the mesh (max Y) centered in X/Z."""
    if not vertices:
        return (0, 0, 0)

    sum_x = sum(v[0] for v in vertices)
    sum_z = sum(v[2] for v in vertices)
    max_y = max(v[1] for v in vertices)
    n = len(vertices)

    return (sum_x / n, max_y, sum_z / n)


def calculate_pivot_from_min_y(vertices):
    """Calculate pivot point at the bottom of the mesh (min Y) centered in X/Z."""
    if not vertices:
        return (0, 0, 0)

    sum_x = sum(v[0] for v in vertices)
    sum_z = sum(v[2] for v in vertices)
    min_y = min(v[1] for v in vertices)
    n = len(vertices)

    return (sum_x / n, min_y, sum_z / n)


# Default character part definitions for offset calculation
# Each part has: object_name_pattern, pivot_type ('center', 'top', 'bottom')
DEFAULT_CHARACTER_PARTS = {
    'body': ('body', 'center'),
    'head': ('head', 'bottom'),      # Pivot at neck (bottom of head)
    'arm_left': ('arm_L', 'top'),    # Pivot at shoulder (top of arm)
    'arm_right': ('arm_R', 'top'),
    'leg_left': ('leg_L', 'top'),    # Pivot at hip (top of leg)
    'leg_right': ('leg_R', 'top'),
}


def parse_part_mappings(mappings_list):
    """Parse part mappings from command line args.

    Format: part_name=object_pattern (e.g., 'body=kid_body_F_L')
    """
    parts = dict(DEFAULT_CHARACTER_PARTS)  # Start with defaults

    if not mappings_list:
        return parts

    for mapping in mappings_list:
        if '=' not in mapping:
            print(f"Warning: Invalid mapping '{mapping}', expected 'part=object_name'")
            continue

        part_name, obj_pattern = mapping.split('=', 1)
        part_name = part_name.strip().lower()

        # Determine pivot type based on part name
        if part_name == 'body':
            pivot_type = 'center'
        elif part_name == 'head':
            pivot_type = 'bottom'
        else:
            pivot_type = 'top'  # Arms and legs pivot at top

        parts[part_name] = (obj_pattern.strip(), pivot_type)

    return parts


def get_object_vertices(objects, all_vertices, object_pattern):
    """Get vertices for an object by pattern name."""
    matches = find_matching_objects(objects, object_pattern)
    if not matches:
        return []

    vertices = []
    for name in matches:
        for global_idx in objects[name]['vertex_indices']:
            vertices.append(all_vertices[global_idx])
    return vertices


def calculate_part_pivot(vertices, pivot_type):
    """Calculate pivot point for a part based on pivot type."""
    if pivot_type == 'center':
        return calculate_center(vertices)
    elif pivot_type == 'top':
        return calculate_pivot_from_max_y(vertices)
    elif pivot_type == 'bottom':
        return calculate_pivot_from_min_y(vertices)
    else:
        return calculate_center(vertices)


def calculate_bounds(vertices):
    """Calculate bounding box of vertices."""
    if not vertices:
        return None

    min_x = min(v[0] for v in vertices)
    max_x = max(v[0] for v in vertices)
    min_y = min(v[1] for v in vertices)
    max_y = max(v[1] for v in vertices)
    min_z = min(v[2] for v in vertices)
    max_z = max(v[2] for v in vertices)

    return {
        'min': (min_x, min_y, min_z),
        'max': (max_x, max_y, max_z),
        'center': ((min_x + max_x) / 2, (min_y + max_y) / 2, (min_z + max_z) / 2)
    }


def calculate_character_offsets(filepath, scale=28.0, part_mappings=None):
    """Calculate all part offsets from body center to each part's pivot.

    Uses each part's actual pivot position (top/bottom/center) relative to body center.
    Returns a dict of part_name -> (offset_x, offset_y, offset_z) in PS1 coordinates.
    PS1 coords: X=right, Y=down (negative=up), Z=into screen
    """
    parts = part_mappings if part_mappings else DEFAULT_CHARACTER_PARTS

    objects, all_vertices, all_uvs, all_colors = parse_obj_by_object(filepath)

    # Get body center as reference
    body_pattern, body_pivot_type = parts['body']
    body_vertices = get_object_vertices(objects, all_vertices, body_pattern)
    if not body_vertices:
        print(f"Error: Could not find body object '{body_pattern}'")
        return None

    body_center = calculate_part_pivot(body_vertices, body_pivot_type)
    print(f"Body center (OBJ): ({body_center[0]:.3f}, {body_center[1]:.3f}, {body_center[2]:.3f})")

    offsets = {}

    for part_name, (obj_pattern, pivot_type) in parts.items():
        part_vertices = get_object_vertices(objects, all_vertices, obj_pattern)
        if not part_vertices:
            print(f"  Warning: Could not find {part_name} object '{obj_pattern}'")
            offsets[part_name] = (0, 0, 0)
            continue

        # Calculate this part's pivot point based on its pivot type
        part_pivot = calculate_part_pivot(part_vertices, pivot_type)

        # Calculate offset from body center to part pivot (in OBJ coords)
        obj_offset_x = part_pivot[0] - body_center[0]
        obj_offset_y = part_pivot[1] - body_center[1]
        obj_offset_z = part_pivot[2] - body_center[2]

        # Body is always at origin
        if part_name == 'body':
            obj_offset_x = obj_offset_y = obj_offset_z = 0

        # Convert to PS1 coordinates:
        # PS1 X = OBJ X (right)
        # PS1 Y = -OBJ Y (down, so negate)
        # PS1 Z = OBJ Z (into screen)
        ps1_offset_x = int(obj_offset_x * scale)
        ps1_offset_y = int(-obj_offset_y * scale)  # Negate Y
        ps1_offset_z = int(obj_offset_z * scale)

        offsets[part_name] = (ps1_offset_x, ps1_offset_y, ps1_offset_z)

        print(f"  {part_name}: pivot ({part_pivot[0]:.3f}, {part_pivot[1]:.3f}, {part_pivot[2]:.3f}) "
              f"-> offset PS1 ({ps1_offset_x}, {ps1_offset_y}, {ps1_offset_z})")

    return offsets


def write_offsets_header(offsets, output_path, char_name):
    """Write offsets to a header file."""
    # Convert character name to uppercase for defines
    prefix = char_name.upper().replace('-', '_').replace(' ', '_')

    lines = [
        f"/* Auto-generated character offsets for {char_name} */",
        f"/* Generated by convertCharacter.py --calculate-offsets */",
        "",
        f"#pragma once",
        "",
        f"/* Part offsets (PS1 coords: X=right, Y=down, Z=forward) */",
        f"#define {prefix}_BODY_OFFSET_X {offsets['body'][0]}",
        f"#define {prefix}_BODY_OFFSET_Y {offsets['body'][1]}",
        f"#define {prefix}_BODY_OFFSET_Z {offsets['body'][2]}",
        "",
        f"#define {prefix}_HEAD_OFFSET_X {offsets['head'][0]}",
        f"#define {prefix}_HEAD_OFFSET_Y {offsets['head'][1]}",
        f"#define {prefix}_HEAD_OFFSET_Z {offsets['head'][2]}",
        "",
        f"#define {prefix}_ARM_LEFT_OFFSET_X {offsets['arm_left'][0]}",
        f"#define {prefix}_ARM_LEFT_OFFSET_Y {offsets['arm_left'][1]}",
        f"#define {prefix}_ARM_LEFT_OFFSET_Z {offsets['arm_left'][2]}",
        "",
        f"#define {prefix}_ARM_RIGHT_OFFSET_X {offsets['arm_right'][0]}",
        f"#define {prefix}_ARM_RIGHT_OFFSET_Y {offsets['arm_right'][1]}",
        f"#define {prefix}_ARM_RIGHT_OFFSET_Z {offsets['arm_right'][2]}",
        "",
        f"#define {prefix}_LEG_LEFT_OFFSET_X {offsets['leg_left'][0]}",
        f"#define {prefix}_LEG_LEFT_OFFSET_Y {offsets['leg_left'][1]}",
        f"#define {prefix}_LEG_LEFT_OFFSET_Z {offsets['leg_left'][2]}",
        "",
        f"#define {prefix}_LEG_RIGHT_OFFSET_X {offsets['leg_right'][0]}",
        f"#define {prefix}_LEG_RIGHT_OFFSET_Y {offsets['leg_right'][1]}",
        f"#define {prefix}_LEG_RIGHT_OFFSET_Z {offsets['leg_right'][2]}",
        "",
    ]

    Path(output_path).parent.mkdir(parents=True, exist_ok=True)
    with open(output_path, 'w') as f:
        f.write('\n'.join(lines))

    print(f"Wrote offsets to {output_path}")


def main():
    parser = argparse.ArgumentParser(description='Convert OBJ objects to PS1 binary format')
    parser.add_argument('input', help='Input OBJ file')
    parser.add_argument('output', nargs='?', help='Output binary file (not required for --calculate-offsets)')
    parser.add_argument('-o', '--objects', nargs='+',
                        help='Object name(s) to extract')
    parser.add_argument('-s', '--scale', type=float, default=28.0,
                        help='Scale factor for vertices (default: 28.0)')
    parser.add_argument('-t', '--texsize', type=int, default=64,
                        help='Texture size in pixels (default: 64)')
    parser.add_argument('--no-center', action='store_true',
                        help='Do not center the model at origin')
    parser.add_argument('--pivot', type=str, default=None,
                        help='Pivot point: "center" (default), "top", "bottom", or "x,y,z" coordinates')
    parser.add_argument('--list-objects', action='store_true',
                        help='List all objects in the file and exit')
    parser.add_argument('--calculate-offsets', action='store_true',
                        help='Calculate part offsets relative to body center')
    parser.add_argument('--offsets-output', type=str, metavar='FILE',
                        help='Output header file for offsets (used with --calculate-offsets)')
    parser.add_argument('--char-name', type=str, default='CHARACTER',
                        help='Character name for header defines (default: CHARACTER)')
    parser.add_argument('--part', nargs='+', metavar='PART=OBJ',
                        help='Map part name to object pattern (e.g., body=kid_body head=kid_head)')

    args = parser.parse_args()

    # Handle --calculate-offsets mode
    if args.calculate_offsets:
        print(f"Calculating character offsets from {args.input}...")
        part_mappings = parse_part_mappings(args.part)
        offsets = calculate_character_offsets(args.input, args.scale, part_mappings)
        if offsets:
            if args.offsets_output:
                write_offsets_header(offsets, args.offsets_output, args.char_name)
            else:
                # Print to stdout if no output file specified
                for part_name, (x, y, z) in offsets.items():
                    print(f"  {part_name}: ({x}, {y}, {z})")
        return

    # Validate required args for conversion mode
    if not args.output:
        parser.error("output is required for conversion (use --calculate-offsets to skip)")
    if not args.objects:
        parser.error("-o/--objects is required for conversion")

    print(f"Parsing {args.input}...")
    objects, all_vertices, all_uvs, all_colors = parse_obj_by_object(args.input)

    if args.list_objects:
        print("Objects in file:")
        for name in objects:
            obj = objects[name]
            print(f"  {name}: {len(obj['vertex_indices'])} vertices, {len(obj['faces'])} faces")
        return

    print(f"Extracting objects: {args.objects}")
    vertices, uvs, colors, faces = extract_object(objects, all_vertices, all_uvs, all_colors, args.objects)

    # Get pivot object vertices (last object) for pivot calculation
    pivot_vertices = get_pivot_object_vertices(objects, all_vertices, args.objects)

    print(f"  Vertices: {len(vertices)}")
    print(f"  UVs: {len(uvs)}")
    print(f"  Faces: {len(faces)}")

    # Determine pivot/center point (use primary object only to keep pivot stable)
    if args.no_center:
        center = None
    elif args.pivot:
        if args.pivot == 'center':
            center = calculate_center(pivot_vertices)
        elif args.pivot == 'top':
            center = calculate_pivot_from_max_y(pivot_vertices)
        elif args.pivot == 'bottom':
            center = calculate_pivot_from_min_y(pivot_vertices)
        else:
            # Parse x,y,z coordinates
            try:
                parts = args.pivot.split(',')
                center = (float(parts[0]), float(parts[1]), float(parts[2]))
            except:
                print(f"Error: Invalid pivot format '{args.pivot}'. Use 'center', 'top', 'bottom', or 'x,y,z'")
                return
    else:
        center = calculate_center(pivot_vertices)

    if center:
        print(f"  Pivot: ({center[0]:.2f}, {center[1]:.2f}, {center[2]:.2f})")

    binary_data, vertex_colors = convert_to_binary(
        vertices, uvs, colors, faces,
        args.scale, args.texsize, center,
        use_vertex_colors=True
    )
    print(f"  Output size: {len(binary_data)} bytes")

    # Ensure output directory exists
    Path(args.output).parent.mkdir(parents=True, exist_ok=True)

    with open(args.output, 'wb') as f:
        f.write(binary_data)

    print("Done!")


if __name__ == '__main__':
    main()
