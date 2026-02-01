/*
 * Camera System for PS1 bare-metal
 *
 * View Matrix Construction:
 *
 * The view matrix transforms world coordinates to camera-relative coordinates.
 * It consists of:
 *   1. Translation: Move world so camera is at origin
 *   2. Rotation: Rotate world so camera looks down -Z axis
 *
 * For a camera with yaw (Y rotation) and pitch (X rotation):
 *
 * View = RotX(pitch) * RotY(yaw) * Translate(-camPos)
 *
 * The rotation part (combined yaw then pitch) is:
 *
 * Row 0: [ cos(yaw),              0,              sin(yaw)          ]
 * Row 1: [ sin(yaw)*sin(pitch),   cos(pitch),    -cos(yaw)*sin(pitch)]
 * Row 2: [-sin(yaw)*cos(pitch),   sin(pitch),     cos(yaw)*cos(pitch)]
 */

#include "camera.h"
#include "trig.h"
#include "ps1/gte.h"

void cameraInit(Camera *cam, int32_t x, int32_t y, int32_t z) {
	cam->x = x;
	cam->y = y;
	cam->z = z;
	cam->pitch = 0;
	cam->yaw = 0;

	/* Initialize to default forward direction */
	cam->forwardX = 0;
	cam->forwardY = 0;
	cam->forwardZ = FP_ONE;

	cam->rightX = FP_ONE;
	cam->rightY = 0;
	cam->rightZ = 0;

	cam->upX = 0;
	cam->upY = FP_ONE;
	cam->upZ = 0;

	/* Initialize view matrix to identity */
	matrixIdentity(&cam->viewRotation);
	cam->viewTX = -x;
	cam->viewTY = -y;
	cam->viewTZ = -z;
}

void cameraSetRotation(Camera *cam, int16_t pitch, int16_t yaw) {
	cam->pitch = pitch;
	cam->yaw = yaw;
	cameraUpdateVectors(cam);
	cameraUpdateViewMatrix(cam);
}

void cameraUpdateVectors(Camera *cam) {
	/*
	 * Calculate direction vectors from pitch/yaw.
	 *
	 * Forward vector (where camera looks):
	 *   x = sin(yaw) * cos(pitch)
	 *   y = -sin(pitch)
	 *   z = cos(yaw) * cos(pitch)
	 *
	 * Note: Both PS1 angles and isin/icos use 4096 = 360 degrees
	 */
	int32_t cy = icos(cam->yaw);
	int32_t sy = isin(cam->yaw);
	int32_t cp = icos(cam->pitch);
	int32_t sp = isin(cam->pitch);

	/* Forward vector */
	cam->forwardX = (sy * cp) >> FP_SHIFT;
	cam->forwardY = -sp;
	cam->forwardZ = (cy * cp) >> FP_SHIFT;

	/* Right vector = forward cross worldUp, then normalize
	 * For a Y-up system with yaw only, right is simply:
	 *   x = cos(yaw)
	 *   y = 0
	 *   z = -sin(yaw)
	 */
	cam->rightX = cy;
	cam->rightY = 0;
	cam->rightZ = -sy;

	/* Up vector = right cross forward
	 * For pitch/yaw camera:
	 *   x = sin(yaw) * sin(pitch)
	 *   y = cos(pitch)
	 *   z = cos(yaw) * sin(pitch)
	 */
	cam->upX = (sy * sp) >> FP_SHIFT;
	cam->upY = cp;
	cam->upZ = (cy * sp) >> FP_SHIFT;
}

void cameraUpdateViewMatrix(Camera *cam) {
	/*
	 * Build the view rotation matrix.
	 *
	 * Camera rotation is R = RotY(yaw) * RotX(pitch) (yaw first, then pitch).
	 * View matrix is the inverse: R^(-1) = RotX(-pitch) * RotY(-yaw).
	 *
	 * Using cos(-θ) = cos(θ), sin(-θ) = -sin(θ):
	 */
	int32_t cy = icos(cam->yaw);
	int32_t sy = isin(cam->yaw);
	int32_t cp = icos(cam->pitch);
	int32_t sp = isin(cam->pitch);

	/* VIEW matrix = RotX(-pitch) * RotY(-yaw)
	 *
	 * Row 0: [cy, 0, -sy] */
	cam->viewRotation.m[0][0] = cy;
	cam->viewRotation.m[0][1] = 0;
	cam->viewRotation.m[0][2] = -sy;

	/* Row 1: [sp*sy, cp, sp*cy] */
	cam->viewRotation.m[1][0] = (sp * sy) >> FP_SHIFT;
	cam->viewRotation.m[1][1] = cp;
	cam->viewRotation.m[1][2] = (sp * cy) >> FP_SHIFT;

	/* Row 2: [cp*sy, -sp, cp*cy] */
	cam->viewRotation.m[2][0] = (cp * sy) >> FP_SHIFT;
	cam->viewRotation.m[2][1] = -sp;
	cam->viewRotation.m[2][2] = (cp * cy) >> FP_SHIFT;

	/* Calculate translated position in view space
	 * viewT = viewRotation * (-camPos)
	 */
	int32_t tx = -cam->x;
	int32_t ty = -cam->y;
	int32_t tz = -cam->z;

	cam->viewTX = ((int32_t)cam->viewRotation.m[0][0] * tx +
	               (int32_t)cam->viewRotation.m[0][1] * ty +
	               (int32_t)cam->viewRotation.m[0][2] * tz) >> FP_SHIFT;

	cam->viewTY = ((int32_t)cam->viewRotation.m[1][0] * tx +
	               (int32_t)cam->viewRotation.m[1][1] * ty +
	               (int32_t)cam->viewRotation.m[1][2] * tz) >> FP_SHIFT;

	cam->viewTZ = ((int32_t)cam->viewRotation.m[2][0] * tx +
	               (int32_t)cam->viewRotation.m[2][1] * ty +
	               (int32_t)cam->viewRotation.m[2][2] * tz) >> FP_SHIFT;
}

void cameraLoadToGTE(const Camera *cam) {
	/* Load rotation matrix */
	matrixLoadToGTE(&cam->viewRotation);

	/* Load translation */
	gte_setControlReg(GTE_TRX, cam->viewTX);
	gte_setControlReg(GTE_TRY, cam->viewTY);
	gte_setControlReg(GTE_TRZ, cam->viewTZ);
}

void cameraTransformPoint(const Camera *cam, int32_t *x, int32_t *y, int32_t *z) {
	/* Apply view rotation then translation */
	int32_t ox = *x, oy = *y, oz = *z;

	*x = ((int32_t)cam->viewRotation.m[0][0] * ox +
	      (int32_t)cam->viewRotation.m[0][1] * oy +
	      (int32_t)cam->viewRotation.m[0][2] * oz) >> FP_SHIFT;
	*x += cam->viewTX;

	*y = ((int32_t)cam->viewRotation.m[1][0] * ox +
	      (int32_t)cam->viewRotation.m[1][1] * oy +
	      (int32_t)cam->viewRotation.m[1][2] * oz) >> FP_SHIFT;
	*y += cam->viewTY;

	*z = ((int32_t)cam->viewRotation.m[2][0] * ox +
	      (int32_t)cam->viewRotation.m[2][1] * oy +
	      (int32_t)cam->viewRotation.m[2][2] * oz) >> FP_SHIFT;
	*z += cam->viewTZ;
}

void cameraLookAt(Camera *cam, int32_t targetX, int32_t targetY, int32_t targetZ) {
	/*
	 * Calculate yaw and pitch to look at target.
	 *
	 * Direction vector from camera to target:
	 *   dx = target.x - cam.x
	 *   dy = target.y - cam.y
	 *   dz = target.z - cam.z
	 *
	 * Yaw = atan2(dx, dz)  (angle in XZ plane)
	 * Pitch = atan2(dy, sqrt(dx*dx + dz*dz))  (vertical angle)
	 */
	int32_t dx = targetX - cam->x;
	int32_t dy = targetY - cam->y;
	int32_t dz = targetZ - cam->z;

	/* Calculate yaw using atan2 approximation
	 * PS1 angle units: 0-4095 = 0-360 degrees
	 * atan2(dx, dz) gives us the horizontal angle
	 */
	cam->yaw = iatan2(dx, dz);

	/* Calculate horizontal distance for pitch calculation */
	int32_t distXZ = isqrt((dx * dx) + (dz * dz));

	/* Calculate pitch */
	if (distXZ > 0) {
		cam->pitch = iatan2(dy, distXZ);
	} else {
		/* Looking straight up or down */
		cam->pitch = (dy > 0) ? 1024 : -1024;  /* +/- 90 degrees */
	}

	/* Clamp pitch to avoid gimbal lock (just under 90 degrees) */
	if (cam->pitch > 1020) cam->pitch = 1020;
	if (cam->pitch < -1020) cam->pitch = -1020;

	cameraUpdateVectors(cam);
	cameraUpdateViewMatrix(cam);
}

void cameraOrbit(Camera *cam, int32_t targetX, int32_t targetY, int32_t targetZ,
                 int16_t orbitAngle, int32_t distance, int32_t height) {
	/*
	 * Position camera on a circle around the target.
	 *
	 * Camera position:
	 *   x = target.x + sin(angle) * distance
	 *   y = target.y + height
	 *   z = target.z + cos(angle) * distance
	 *
	 * Then look at the target.
	 */
	int32_t cosA = icos(orbitAngle);
	int32_t sinA = isin(orbitAngle);

	cam->x = targetX + ((sinA * distance) >> FP_SHIFT);
	cam->y = targetY + height;
	cam->z = targetZ + ((cosA * distance) >> FP_SHIFT);

	/* Look at the target */
	cameraLookAt(cam, targetX, targetY, targetZ);
}

void cameraAddPitch(Camera *cam, int16_t pitchOffset) {
	cam->pitch += pitchOffset;

	/* Clamp pitch to avoid gimbal lock (just under 90 degrees) */
	if (cam->pitch > 1020) cam->pitch = 1020;
	if (cam->pitch < -1020) cam->pitch = -1020;

	cameraUpdateVectors(cam);
	cameraUpdateViewMatrix(cam);
}
