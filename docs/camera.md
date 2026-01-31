# Camera Projection System

This document explains how the 3D camera and projection system works for PS1 bare-metal development.

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

The view matrix transforms world coordinates into camera-relative coordinates. It combines:
1. **Translation** by negative camera position
2. **Rotation** by yaw (Y-axis) then pitch (X-axis)

```c
void cameraUpdateViewMatrix(Camera *cam) {
    int32_t cy = icos(cam->yaw);
    int32_t sy = isin(cam->yaw);
    int32_t cp = icos(cam->pitch);
    int32_t sp = isin(cam->pitch);

    /* Combined matrix M = RotX(pitch) * RotY(yaw):
     *
     * Row 0: [cos(yaw),              0,              sin(yaw)           ]
     * Row 1: [sin(yaw)*sin(pitch),   cos(pitch),    -cos(yaw)*sin(pitch)]
     * Row 2: [-sin(yaw)*cos(pitch),  sin(pitch),     cos(yaw)*cos(pitch)]
     */
    cam->viewRotation.m[0][0] = cy;
    cam->viewRotation.m[0][1] = 0;
    cam->viewRotation.m[0][2] = sy;

    cam->viewRotation.m[1][0] = (sy * sp) >> FP_SHIFT;
    cam->viewRotation.m[1][1] = cp;
    cam->viewRotation.m[1][2] = -(cy * sp) >> FP_SHIFT;

    cam->viewRotation.m[2][0] = -(sy * cp) >> FP_SHIFT;
    cam->viewRotation.m[2][1] = sp;
    cam->viewRotation.m[2][2] = (cy * cp) >> FP_SHIFT;

    /* Translation in view space: viewT = viewRotation * (-camPos) */
    int32_t tx = -cam->x;
    int32_t ty = -cam->y;
    int32_t tz = -cam->z;

    cam->viewTX = (cam->viewRotation.m[0][0] * tx + ... ) >> FP_SHIFT;
    cam->viewTY = (cam->viewRotation.m[1][0] * tx + ... ) >> FP_SHIFT;
    cam->viewTZ = (cam->viewRotation.m[2][0] * tx + ... ) >> FP_SHIFT;
}
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

## Notes

- The PS1 GTE handles perspective projection automatically
- Matrices use **row-major** storage (m[row][col])
- Near/far clipping is done in software using Z values
- The ordering table handles depth sorting for correct overlap
