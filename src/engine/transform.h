/*
 * Transform math module for PS1 bare-metal
 * Handles matrices, rotations, and hierarchical transforms
 *
 * Coordinate system (Y-up, left-handed like PS1/DirectX):
 *   X = right
 *   Y = up
 *   Z = into screen (forward)
 *
 * Rotation conventions:
 *   Yaw   = rotation around Y axis (turning left/right)
 *   Pitch = rotation around X axis (looking up/down, limb swing)
 *   Roll  = rotation around Z axis (tilting sideways)
 */

#pragma once

#include <stdint.h>
#include "ps1/gte.h"

/* Fixed-point format (20.12) matching GTE */
#define FP_ONE (1 << 12)
#define FP_SHIFT 12

/* 3x3 rotation matrix (row-major storage) */
typedef struct {
	int16_t m[3][3];  /* m[row][col] */
} Matrix3x3;

/* Transform: rotation + translation */
typedef struct {
	Matrix3x3 rotation;
	int32_t tx, ty, tz;
} Transform;

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize identity matrix */
void matrixIdentity(Matrix3x3 *m);

/* Create rotation matrices (angles in PS1 angle units: 0-4095 = 0-360 degrees) */
void matrixRotateX(Matrix3x3 *m, int angle);  /* Pitch */
void matrixRotateY(Matrix3x3 *m, int angle);  /* Yaw */
void matrixRotateZ(Matrix3x3 *m, int angle);  /* Roll */

/* Multiply two matrices: result = a * b */
void matrixMultiply(Matrix3x3 *result, const Matrix3x3 *a, const Matrix3x3 *b);

/* Apply rotation to existing matrix: m = m * rotation */
void matrixApplyRotateX(Matrix3x3 *m, int angle);
void matrixApplyRotateY(Matrix3x3 *m, int angle);
void matrixApplyRotateZ(Matrix3x3 *m, int angle);

/* Rotate a point by a matrix */
void matrixTransformPoint(const Matrix3x3 *m, int32_t *x, int32_t *y, int32_t *z);

/* Load matrix into GTE rotation registers */
void matrixLoadToGTE(const Matrix3x3 *m);

/* Build a complete transform matrix with yaw, pitch, roll */
void matrixFromEuler(Matrix3x3 *m, int yaw, int pitch, int roll);

/* Transform operations */
void transformIdentity(Transform *t);
void transformSetRotation(Transform *t, int yaw, int pitch, int roll);
void transformSetTranslation(Transform *t, int32_t x, int32_t y, int32_t z);

/* Combine child transform with parent: result = parent * child */
void transformCombine(Transform *result, const Transform *parent, const Transform *child);

/* Load transform into GTE */
void transformLoadToGTE(const Transform *t);

#ifdef __cplusplus
}
#endif
