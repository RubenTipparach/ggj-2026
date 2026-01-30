/*
 * Transform math module for PS1 bare-metal
 *
 * Standard 3D rotation matrices (right-handed, counterclockwise positive):
 *
 * Rotation around X axis (Pitch):
 *   | 1    0       0    |
 *   | 0   cos   -sin    |
 *   | 0   sin    cos    |
 *
 * Rotation around Y axis (Yaw):
 *   | cos    0    sin   |
 *   |  0     1     0    |
 *   |-sin    0    cos   |
 *
 * Rotation around Z axis (Roll):
 *   | cos  -sin    0    |
 *   | sin   cos    0    |
 *   |  0     0     1    |
 *
 * Reference: https://en.wikipedia.org/wiki/Rotation_matrix
 */

#include "transform.h"
#include "trig.h"

void matrixIdentity(Matrix3x3 *m) {
	m->m[0][0] = FP_ONE; m->m[0][1] = 0;      m->m[0][2] = 0;
	m->m[1][0] = 0;      m->m[1][1] = FP_ONE; m->m[1][2] = 0;
	m->m[2][0] = 0;      m->m[2][1] = 0;      m->m[2][2] = FP_ONE;
}

/* Rotation around X axis (Pitch) - rotates Y toward Z */
void matrixRotateX(Matrix3x3 *m, int angle) {
	int s = isin(angle);
	int c = icos(angle);

	m->m[0][0] = FP_ONE; m->m[0][1] = 0;  m->m[0][2] = 0;
	m->m[1][0] = 0;      m->m[1][1] = c;  m->m[1][2] = -s;
	m->m[2][0] = 0;      m->m[2][1] = s;  m->m[2][2] = c;
}

/* Rotation around Y axis (Yaw) - rotates Z toward X */
void matrixRotateY(Matrix3x3 *m, int angle) {
	int s = isin(angle);
	int c = icos(angle);

	m->m[0][0] = c;  m->m[0][1] = 0;      m->m[0][2] = s;
	m->m[1][0] = 0;  m->m[1][1] = FP_ONE; m->m[1][2] = 0;
	m->m[2][0] = -s; m->m[2][1] = 0;      m->m[2][2] = c;
}

/* Rotation around Z axis (Roll) - rotates X toward Y */
void matrixRotateZ(Matrix3x3 *m, int angle) {
	int s = isin(angle);
	int c = icos(angle);

	m->m[0][0] = c;  m->m[0][1] = -s; m->m[0][2] = 0;
	m->m[1][0] = s;  m->m[1][1] = c;  m->m[1][2] = 0;
	m->m[2][0] = 0;  m->m[2][1] = 0;  m->m[2][2] = FP_ONE;
}

/* Multiply two matrices: result = a * b */
void matrixMultiply(Matrix3x3 *result, const Matrix3x3 *a, const Matrix3x3 *b) {
	Matrix3x3 temp;

	for (int row = 0; row < 3; row++) {
		for (int col = 0; col < 3; col++) {
			int32_t sum = 0;
			for (int k = 0; k < 3; k++) {
				sum += ((int32_t)a->m[row][k] * b->m[k][col]) >> FP_SHIFT;
			}
			temp.m[row][col] = (int16_t)sum;
		}
	}

	*result = temp;
}

/* Apply rotation to existing matrix: m = m * rotationX */
void matrixApplyRotateX(Matrix3x3 *m, int angle) {
	if (angle == 0) return;
	Matrix3x3 rot;
	matrixRotateX(&rot, angle);
	matrixMultiply(m, m, &rot);
}

void matrixApplyRotateY(Matrix3x3 *m, int angle) {
	if (angle == 0) return;
	Matrix3x3 rot;
	matrixRotateY(&rot, angle);
	matrixMultiply(m, m, &rot);
}

void matrixApplyRotateZ(Matrix3x3 *m, int angle) {
	if (angle == 0) return;
	Matrix3x3 rot;
	matrixRotateZ(&rot, angle);
	matrixMultiply(m, m, &rot);
}

/* Rotate a point by a matrix */
void matrixTransformPoint(const Matrix3x3 *m, int32_t *x, int32_t *y, int32_t *z) {
	int32_t ox = *x, oy = *y, oz = *z;

	*x = ((int32_t)m->m[0][0] * ox + (int32_t)m->m[0][1] * oy + (int32_t)m->m[0][2] * oz) >> FP_SHIFT;
	*y = ((int32_t)m->m[1][0] * ox + (int32_t)m->m[1][1] * oy + (int32_t)m->m[1][2] * oz) >> FP_SHIFT;
	*z = ((int32_t)m->m[2][0] * ox + (int32_t)m->m[2][1] * oy + (int32_t)m->m[2][2] * oz) >> FP_SHIFT;
}

/* Load matrix into GTE rotation registers
 * GTE expects column-major format, so we transpose while loading */
void matrixLoadToGTE(const Matrix3x3 *m) {
	/* GTE rotation matrix is stored as:
	 * R11 R12 R13
	 * R21 R22 R23
	 * R31 R32 R33
	 * We load row-major directly since gte_setRotationMatrix takes row-major */
	gte_setRotationMatrix(
		m->m[0][0], m->m[0][1], m->m[0][2],
		m->m[1][0], m->m[1][1], m->m[1][2],
		m->m[2][0], m->m[2][1], m->m[2][2]
	);
}

/* Build rotation matrix from Euler angles (YXZ order - common for games)
 * First yaw (turn), then pitch (look up/down), then roll (tilt) */
void matrixFromEuler(Matrix3x3 *m, int yaw, int pitch, int roll) {
	matrixIdentity(m);

	/* Apply in YXZ order (common for character controllers) */
	if (yaw != 0)   matrixApplyRotateY(m, yaw);
	if (pitch != 0) matrixApplyRotateX(m, pitch);
	if (roll != 0)  matrixApplyRotateZ(m, roll);
}

/* Transform operations */
void transformIdentity(Transform *t) {
	matrixIdentity(&t->rotation);
	t->tx = 0;
	t->ty = 0;
	t->tz = 0;
}

void transformSetRotation(Transform *t, int yaw, int pitch, int roll) {
	matrixFromEuler(&t->rotation, yaw, pitch, roll);
}

void transformSetTranslation(Transform *t, int32_t x, int32_t y, int32_t z) {
	t->tx = x;
	t->ty = y;
	t->tz = z;
}

/* Combine child transform with parent: result = parent * child
 *
 * For hierarchical transforms (like character limbs):
 * 1. First apply child's local rotation
 * 2. Then apply parent's rotation
 * 3. Rotate child's translation by parent's rotation, add to parent's translation
 */
void transformCombine(Transform *result, const Transform *parent, const Transform *child) {
	/* Combined rotation = parent * child */
	matrixMultiply(&result->rotation, &parent->rotation, &child->rotation);

	/* Transform child's translation by parent's rotation, then add parent's translation */
	int32_t cx = child->tx, cy = child->ty, cz = child->tz;
	matrixTransformPoint(&parent->rotation, &cx, &cy, &cz);

	result->tx = parent->tx + cx;
	result->ty = parent->ty + cy;
	result->tz = parent->tz + cz;
}

/* Load transform into GTE */
void transformLoadToGTE(const Transform *t) {
	matrixLoadToGTE(&t->rotation);
	gte_setControlReg(GTE_TRX, t->tx);
	gte_setControlReg(GTE_TRY, t->ty);
	gte_setControlReg(GTE_TRZ, t->tz);
}
