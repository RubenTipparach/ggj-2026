/*
 * Controller Input - PS1 controller polling and state management
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

/* Controller button definitions */
#define PAD_SELECT   (1 << 0)
#define PAD_L3       (1 << 1)
#define PAD_R3       (1 << 2)
#define PAD_START    (1 << 3)
#define PAD_UP       (1 << 4)
#define PAD_RIGHT    (1 << 5)
#define PAD_DOWN     (1 << 6)
#define PAD_LEFT     (1 << 7)
#define PAD_L2       (1 << 8)
#define PAD_R2       (1 << 9)
#define PAD_L1       (1 << 10)
#define PAD_R1       (1 << 11)
#define PAD_TRIANGLE (1 << 12)
#define PAD_CIRCLE   (1 << 13)
#define PAD_X        (1 << 14)
#define PAD_SQUARE   (1 << 15)

/* Controller state structure with analog support */
typedef struct {
	uint16_t buttons;
	uint8_t  leftX;
	uint8_t  leftY;
	uint8_t  rightX;
	uint8_t  rightY;
	bool     isAnalog;
} ControllerState;

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize controller bus for communication */
void initControllerBus(void);

/* Poll controller on specified port (0 or 1) */
void pollController(int port, ControllerState *state);

#ifdef __cplusplus
}
#endif
