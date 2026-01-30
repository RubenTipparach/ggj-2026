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
import struct
import sys
from pathlib import Path


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
    """Extract vertices, uvs, colors, and faces for specified object(s), remapping indices."""
    vertices = []
    uvs = []
    colors = []
    faces = []

    # Collect all vertex and UV indices used by the objects
    vertex_indices = set()
    uv_indices = set()

    for name in object_names:
        if name in objects:
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
    for name in object_names:
        if name in objects:
            for face in objects[name]['faces']:
                remapped_verts = [vertex_map[v] for v in face['verts']]
                remapped_uvs = [uv_map.get(uv, 0) for uv in face['uvs']]
                faces.append({
                    'verts': remapped_verts,
                    'uvs': remapped_uvs
                })

    return vertices, uvs, colors, faces


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


def main():
    parser = argparse.ArgumentParser(description='Convert OBJ objects to PS1 binary format')
    parser.add_argument('input', help='Input OBJ file')
    parser.add_argument('output', help='Output binary file')
    parser.add_argument('-o', '--objects', nargs='+', required=True,
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

    args = parser.parse_args()

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

    print(f"  Vertices: {len(vertices)}")
    print(f"  UVs: {len(uvs)}")
    print(f"  Faces: {len(faces)}")

    # Determine pivot/center point
    if args.no_center:
        center = None
    elif args.pivot:
        if args.pivot == 'center':
            center = calculate_center(vertices)
        elif args.pivot == 'top':
            center = calculate_pivot_from_max_y(vertices)
        elif args.pivot == 'bottom':
            center = calculate_pivot_from_min_y(vertices)
        else:
            # Parse x,y,z coordinates
            try:
                parts = args.pivot.split(',')
                center = (float(parts[0]), float(parts[1]), float(parts[2]))
            except:
                print(f"Error: Invalid pivot format '{args.pivot}'. Use 'center', 'top', 'bottom', or 'x,y,z'")
                return
    else:
        center = calculate_center(vertices)

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
