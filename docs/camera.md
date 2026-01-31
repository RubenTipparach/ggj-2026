# Camera Projection System

This document explains how the 3D camera and projection system works for PS1 bare-metal development.

> **Note**: This documentation reflects lessons learned during development, including
> critical fixes to the view matrix calculation that resolved camera rotation issues.

## Overview

The rendering pipeline transforms 3D world coordinates into 2D screen coordinates through a series of matrix transformations:

```
World Space → View Space → Clip Space → Screen Space
     ↓             ↓            ↓
 (Model)    (View Matrix)  (GTE Projection)
```

## Coordinate System

The engine uses a **right-handed coordinate system** (Y-up):
- **+X**: Right
- **+Y**: Up
- **+Z**: Forward (into the screen from camera's perspective)

## PS1 Angle Units

The PS1 uses a custom angle format for efficiency:
- **0-4095** represents **0-360 degrees**
- **1024** = 90 degrees
- **2048** = 180 degrees
- This maps nicely to 12-bit fixed-point math

## Camera System (camera.h/camera.c)

The camera stores position and orientation using pitch/yaw angles:

```c
typedef struct {
    /* Position in world space */
    int32_t x, y, z;

    /* Orientation (PS1 angle units: 0-4095 = 0-360 degrees) */
    int16_t pitch;  /* X rotation: looking up (-) / down (+) */
    int16_t yaw;    /* Y rotation: looking left (-) / right (+) */

    /* Derived direction vectors (fixed-point 4.12) */
    int16_t forwardX, forwardY, forwardZ;
    int16_t rightX, rightY, rightZ;
    int16_t upX, upY, upZ;

    /* View matrix (3x3 rotation + translation) */
    Matrix3x3 viewRotation;
    int32_t viewTX, viewTY, viewTZ;
} Camera;
```

### Initialization

```c
Camera cam;
cameraInit(&cam, 0, 100, -200);  /* Position: x=0, y=100 up, z=200 behind */
```

### Direction Vectors

When pitch/yaw change, the camera's direction vectors are recalculated:

```c
void cameraUpdateVectors(Camera *cam) {
    int32_t cy = icos(cam->yaw);
    int32_t sy = isin(cam->yaw);
    int32_t cp = icos(cam->pitch);
    int32_t sp = isin(cam->pitch);

    /* Forward vector: where camera looks */
    cam->forwardX = (sy * cp) >> FP_SHIFT;
    cam->forwardY = -sp;
    cam->forwardZ = (cy * cp) >> FP_SHIFT;

    /* Right vector (perpendicular to forward in XZ plane) */
    cam->rightX = cy;
    cam->rightY = 0;
    cam->rightZ = -sy;

    /* Up vector (perpendicular to both) */
    cam->upX = (sy * sp) >> FP_SHIFT;
    cam->upY = cp;
    cam->upZ = (cy * sp) >> FP_SHIFT;
}
```

### View Matrix

The view matrix transforms world coordinates into camera-relative coordinates. This is the
**inverse** of the camera's own rotation - a critical distinction that caused issues during
development.

#### Understanding Camera vs View Rotation

```
Camera Rotation:  How the camera is oriented in world space
                  R_camera = RotY(yaw) * RotX(pitch)

View Matrix:      The INVERSE - rotates the world so camera looks down -Z
                  R_view = R_camera^(-1) = RotX(-pitch) * RotY(-yaw)
```

**Key insight**: The view matrix is NOT the camera rotation. It's the inverse.
For a rotation matrix, inverse = transpose, but the multiplication ORDER must also reverse.

#### Common Mistake (What NOT to do)

A common mistake is to compute the camera rotation directly or simply transpose it:

```c
/* WRONG: This computes camera rotation, not view matrix */
/* M = RotX(pitch) * RotY(yaw) */
cam->viewRotation.m[0][0] = cy;
cam->viewRotation.m[0][2] = sy;   /* <-- wrong sign */
/* ... this will cause objects to rotate opposite to camera */
```

#### Correct Implementation

```c
void cameraUpdateViewMatrix(Camera *cam) {
    int32_t cy = icos(cam->yaw);
    int32_t sy = isin(cam->yaw);
    int32_t cp = icos(cam->pitch);
    int32_t sp = isin(cam->pitch);

    /* VIEW matrix = RotX(-pitch) * RotY(-yaw)
     * Using cos(-θ) = cos(θ), sin(-θ) = -sin(θ):
     *
     * Row 0: [cy, 0, -sy]
     * Row 1: [sp*sy, cp, sp*cy]
     * Row 2: [cp*sy, -sp, cp*cy]
     */
    cam->viewRotation.m[0][0] = cy;
    cam->viewRotation.m[0][1] = 0;
    cam->viewRotation.m[0][2] = -sy;  /* Note: NEGATIVE */

    cam->viewRotation.m[1][0] = (sp * sy) >> FP_SHIFT;
    cam->viewRotation.m[1][1] = cp;
    cam->viewRotation.m[1][2] = (sp * cy) >> FP_SHIFT;

    cam->viewRotation.m[2][0] = (cp * sy) >> FP_SHIFT;
    cam->viewRotation.m[2][1] = -sp;  /* Note: NEGATIVE */
    cam->viewRotation.m[2][2] = (cp * cy) >> FP_SHIFT;

    /* Translation in view space: viewT = viewRotation * (-camPos) */
    int32_t tx = -cam->x;
    int32_t ty = -cam->y;
    int32_t tz = -cam->z;

    cam->viewTX = (cam->viewRotation.m[0][0] * tx +
                   cam->viewRotation.m[0][1] * ty +
                   cam->viewRotation.m[0][2] * tz) >> FP_SHIFT;
    cam->viewTY = (cam->viewRotation.m[1][0] * tx +
                   cam->viewRotation.m[1][1] * ty +
                   cam->viewRotation.m[1][2] * tz) >> FP_SHIFT;
    cam->viewTZ = (cam->viewRotation.m[2][0] * tx +
                   cam->viewRotation.m[2][1] * ty +
                   cam->viewRotation.m[2][2] * tz) >> FP_SHIFT;
}
```

#### Derivation

Given camera rotation `R = RotY(yaw) * RotX(pitch)`:

1. The inverse of a rotation matrix is its transpose
2. But for combined rotations: `(A * B)^(-1) = B^(-1) * A^(-1)`
3. So: `R^(-1) = RotX(-pitch) * RotY(-yaw)` (reverse order, negate angles)

Expanding `RotX(-pitch) * RotY(-yaw)`:

```
RotX(-p) = | 1    0     0  |    RotY(-y) = | cy   0   -sy |
           | 0   cp    sp  |               |  0   1    0  |
           | 0  -sp    cp  |               | sy   0   cy  |

View = RotX(-p) * RotY(-y) =

| 1    0     0  |   | cy   0   -sy |   | cy      0     -sy    |
| 0   cp    sp  | * |  0   1    0  | = | sp*sy   cp    sp*cy  |
| 0  -sp    cp  |   | sy   0   cy  |   | cp*sy  -sp    cp*cy  |
```

## Fixed-Point Math

All calculations use **4.12 fixed-point** format (same as GTE):
- `FP_ONE = 4096` (1.0 in fixed-point)
- `FP_SHIFT = 12` (bits for fractional part)

```c
/* Multiply two fixed-point values */
int32_t result = (a * b) >> FP_SHIFT;

/* Convert integer to fixed-point */
int32_t fp_value = integer_value << FP_SHIFT;

/* Convert fixed-point to integer */
int integer_value = fp_value >> FP_SHIFT;
```

## Rotation Matrices

Standard 3D rotation matrices (right-handed, counterclockwise positive):

### Rotation around X axis (Pitch)
```
| 1    0       0    |
| 0   cos    -sin   |
| 0   sin     cos   |
```

### Rotation around Y axis (Yaw)
```
| cos    0    sin   |
|  0     1     0    |
|-sin    0    cos   |
```

### Rotation around Z axis (Roll)
```
| cos  -sin    0    |
| sin   cos    0    |
|  0     0     1    |
```

## Look-At Function

Point the camera at a specific target:

```c
void cameraLookAt(Camera *cam, int32_t targetX, int32_t targetY, int32_t targetZ) {
    /* Direction from camera to target */
    int32_t dx = targetX - cam->x;
    int32_t dy = targetY - cam->y;
    int32_t dz = targetZ - cam->z;

    /* Yaw = atan2(dx, dz) - horizontal angle */
    cam->yaw = iatan2(dx, dz);

    /* Pitch = atan2(dy, horizontal_distance) */
    int32_t distXZ = isqrt(dx * dx + dz * dz);
    cam->pitch = iatan2(dy, distXZ);

    /* Clamp pitch to avoid gimbal lock */
    if (cam->pitch > 1020) cam->pitch = 1020;   /* ~89 degrees */
    if (cam->pitch < -1020) cam->pitch = -1020;

    cameraUpdateVectors(cam);
    cameraUpdateViewMatrix(cam);
}
```

## Orbit Camera

For third-person or victory camera that circles around a target:

```c
void cameraOrbit(Camera *cam,
                 int32_t targetX, int32_t targetY, int32_t targetZ,
                 int16_t orbitAngle, int32_t distance, int32_t height) {
    /* Position camera on circle around target */
    int32_t cosA = icos(orbitAngle);
    int32_t sinA = isin(orbitAngle);

    cam->x = targetX + ((sinA * distance) >> FP_SHIFT);
    cam->y = targetY + height;
    cam->z = targetZ + ((cosA * distance) >> FP_SHIFT);

    /* Point camera at target */
    cameraLookAt(cam, targetX, targetY, targetZ);
}
```

### Orbit Camera Diagram

```
        Top View (looking down Y axis)

           -Z
            |
      cam   |   cam
       *----+----*
      /     |     \
     /      |      \
    *   [TARGET]    *  cam
     \      |      /
      \     |     /
       *----+----*
      cam   |   cam
            |
           +Z

    Camera orbits in XZ plane
    at fixed Y offset above target
```

## Loading to GTE

Load camera view matrix into GTE for hardware-accelerated transforms:

```c
void cameraLoadToGTE(const Camera *cam) {
    /* Load 3x3 rotation matrix */
    gte_setRotationMatrix(
        cam->viewRotation.m[0][0], cam->viewRotation.m[0][1], cam->viewRotation.m[0][2],
        cam->viewRotation.m[1][0], cam->viewRotation.m[1][1], cam->viewRotation.m[1][2],
        cam->viewRotation.m[2][0], cam->viewRotation.m[2][1], cam->viewRotation.m[2][2]
    );

    /* Load translation vector */
    gte_setControlReg(GTE_TRX, cam->viewTX);
    gte_setControlReg(GTE_TRY, cam->viewTY);
    gte_setControlReg(GTE_TRZ, cam->viewTZ);
}
```

## Typical Usage

```c
/* Setup */
Camera cam;
cameraInit(&cam, 0, 50, -200);

/* Game loop */
void update() {
    /* Option 1: Manual control */
    if (input_left)  cam.yaw -= 40;
    if (input_right) cam.yaw += 40;
    cameraSetRotation(&cam, cam.pitch, cam.yaw);

    /* Option 2: Follow a target */
    cameraOrbit(&cam, player.x, player.y, player.z,
                orbitAngle, 200, 50);

    /* Option 3: Look at target from current position */
    cameraLookAt(&cam, enemy.x, enemy.y, enemy.z);
}

void render() {
    /* Load camera to GTE before rendering objects */
    cameraLoadToGTE(&cam);

    /* Now all RTPT commands will transform relative to camera */
    drawWorld();
    drawCharacter();
}
```

## Rendering Pipeline Summary

1. **Model Transform**: Object local space → World space (object's position/rotation)
2. **View Transform**: World space → View space (camera at origin, looking down -Z)
3. **GTE Projection**: View space → Screen space (perspective divide, viewport mapping)

```c
/* For each object */
Transform objectTransform;
transformSetTranslation(&objectTransform, object.x, object.y, object.z);
transformSetRotation(&objectTransform, object.yaw, object.pitch, object.roll);

/* Combine with camera view */
Transform combined;
transformCombine(&combined, &cam.viewTransform, &objectTransform);
transformLoadToGTE(&combined);

/* Now GTE commands (RTPT) will project vertices to screen */
for (each vertex) {
    gte_loadV0(&vertex);
    gte_command(GTE_CMD_RTPT);  /* Transform and project */
    /* Read screen coordinates from GTE */
}
```

## Trigonometry Functions

The engine uses custom integer-based trig functions:

```c
int isin(int x);   /* Returns sine * 4096 */
int icos(int x);   /* Returns cosine * 4096 */
int iatan2(int y, int x);  /* Returns angle in PS1 units */
int isqrt(int x);  /* Integer square root */
```

**Critical**: `isin` and `icos` use **4096 units = full circle (360°)**:
- Input angle of 0 → sin=0, cos=4096
- Input angle of 1024 → sin=4096, cos=0 (90 degrees)
- Input angle of 2048 → sin=0, cos=-4096 (180 degrees)

This matches the PS1 angle unit convention. Do NOT divide angles by 2 before passing
to these functions - pass them directly.

## Debugging Tips

### Symptom: Objects rotate multiple times before returning to original position

**Cause**: Angle units mismatch. Check if you're dividing angles when you shouldn't.

**Fix**: Verify that trig functions use 4096 = 360°. Pass angles directly without conversion.

### Symptom: Camera rotation is backwards or inverted

**Cause**: Using camera rotation matrix instead of view matrix (the inverse).

**Fix**: Ensure view matrix uses `RotX(-pitch) * RotY(-yaw)`, not `RotX(pitch) * RotY(yaw)`.

### Symptom: Floor appears slanted when pitching camera

**Cause**: Matrix multiplication order is wrong.

**Fix**: View matrix must be `RotX(-pitch) * RotY(-yaw)`, computed as:
- First build RotX matrix with -pitch
- Then multiply by RotY matrix with -yaw
- Order matters! `A * B ≠ B * A` for rotation matrices

### Symptom: Objects rotate at different rates than the floor

**Cause**: Different transform code paths using inconsistent rotation calculations.

**Fix**: All rendering (floor, characters, objects) must use the same `cam->viewRotation`
matrix. Don't compute separate yaw-only rotations for some objects.

## Common Pitfalls

1. **Ordering table reverse linking**: `allocatePacket` links packets in reverse
   order within each Z index. This means state-setting commands (texpage, etc.)
   should be at a **higher Z index** than their draw commands, otherwise the
   state is set AFTER the drawing occurs.

2. **Matrix order**: `RotX * RotY ≠ RotY * RotX`. Rotation is not commutative.

2. **View vs Camera**: The view matrix is the INVERSE of camera orientation. When
   camera rotates right, the world appears to rotate left.

3. **Inverse of combined rotations**: `(A * B)^(-1) = B^(-1) * A^(-1)` - the order
   reverses AND each matrix is inverted.

4. **Fixed-point overflow**: Always shift after multiplication:
   ```c
   /* Correct */
   result = (a * b) >> FP_SHIFT;

   /* Wrong - may overflow before shift */
   result = a * b >> FP_SHIFT;  /* Operator precedence issue */
   ```

5. **Angle wraparound**: PS1 angles use 12-bit precision (0-4095). They naturally
   wrap, but be careful with comparisons and interpolation.

## Notes

- The PS1 GTE handles perspective projection automatically
- Matrices use **row-major** storage (m[row][col])
- Near/far clipping is done in software using Z values
- The ordering table handles depth sorting for correct overlap
- All trig functions return fixed-point values (multiply by 4096)
- For orbit cameras, the camera looks AT the target after positioning
