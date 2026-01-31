/*
 * Camera System for PS1 bare-metal
 *
 * Provides view matrix calculation and camera positioning.
 * Uses fixed-point math compatible with GTE.
 *
 * Coordinate system (right-handed, Y-up):
 *   +X = Right
 *   +Y = Up
 *   +Z = Forward (into screen from camera's perspective)
 */

#pragma once

#include <stdint.h>
#include "transform.h"

/* Camera structure */
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

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize camera at position */
void cameraInit(Camera *cam, int32_t x, int32_t y, int32_t z);

/* Set camera orientation from pitch/yaw angles */
void cameraSetRotation(Camera *cam, int16_t pitch, int16_t yaw);

/* Update camera direction vectors from current pitch/yaw */
void cameraUpdateVectors(Camera *cam);

/* Calculate view matrix from current position and orientation */
void cameraUpdateViewMatrix(Camera *cam);

/* Load camera view matrix into GTE for rendering */
void cameraLoadToGTE(const Camera *cam);

/* Transform a world-space point to camera view space */
void cameraTransformPoint(const Camera *cam, int32_t *x, int32_t *y, int32_t *z);

/* Look-at: Point camera toward a target position */
void cameraLookAt(Camera *cam, int32_t targetX, int32_t targetY, int32_t targetZ);

/* Orbit camera around a target point */
void cameraOrbit(Camera *cam, int32_t targetX, int32_t targetY, int32_t targetZ,
                 int16_t orbitAngle, int32_t distance, int32_t height);

#ifdef __cplusplus
}
#endif
