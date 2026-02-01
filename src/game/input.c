/*
 * Controller Input Implementation
 */

#include "input.h"
#include "ps1/registers.h"

/* CPU frequency for timing calculations */
#define F_CPU 33868800

/* Delay helper for controller timing */
static void delayMicroseconds(int time)
{
	time = ((time * 271) + 4) / 8;
	__asm__ volatile(
		".set push\n"
		".set noreorder\n"
		"bgtz  %0, .\n"
		"addiu %0, -2\n"
		".set pop\n"
		: "+r"(time)
	);
}

/* Initialize controller bus for communication */
void initControllerBus(void)
{
	SIO_CTRL(0) = SIO_CTRL_RESET;
	SIO_MODE(0) = SIO_MODE_BAUD_DIV1 | SIO_MODE_DATA_8;
	SIO_BAUD(0) = F_CPU / 250000;
	SIO_CTRL(0) = SIO_CTRL_TX_ENABLE | SIO_CTRL_RX_ENABLE | SIO_CTRL_DSR_IRQ_ENABLE;
}

/* Wait for controller acknowledge signal */
static bool waitForAcknowledge(int timeout)
{
	for (; timeout > 0; timeout -= 10) {
		if (IRQ_STAT & (1 << IRQ_SIO0)) {
			IRQ_STAT = ~(1 << IRQ_SIO0);
			SIO_CTRL(0) |= SIO_CTRL_ACKNOWLEDGE;
			return true;
		}
		delayMicroseconds(10);
	}
	return false;
}

/* Exchange a byte with the controller with timeout */
static uint8_t exchangeByteWithTimeout(uint8_t value, int timeout)
{
	while (!(SIO_STAT(0) & SIO_STAT_TX_NOT_FULL)) {
		if (--timeout <= 0) return 0xFF;
		__asm__ volatile("");
	}
	SIO_DATA(0) = value;

	timeout = 10000;
	while (!(SIO_STAT(0) & SIO_STAT_RX_NOT_EMPTY)) {
		if (--timeout <= 0) return 0xFF;
		__asm__ volatile("");
	}
	return SIO_DATA(0);
}

/* Poll controller on specified port (0 or 1) */
void pollController(int port, ControllerState *state)
{
	state->buttons = 0;
	state->leftX = 0x80;
	state->leftY = 0x80;
	state->rightX = 0x80;
	state->rightY = 0x80;
	state->isAnalog = false;

	if (port)
		SIO_CTRL(0) |= SIO_CTRL_CS_PORT_2;
	else
		SIO_CTRL(0) &= ~SIO_CTRL_CS_PORT_2;

	IRQ_STAT = ~(1 << IRQ_SIO0);
	SIO_CTRL(0) |= SIO_CTRL_DTR | SIO_CTRL_ACKNOWLEDGE;
	delayMicroseconds(60);

	SIO_DATA(0) = 0x01;

	if (!waitForAcknowledge(500)) {
		SIO_CTRL(0) &= ~SIO_CTRL_DTR;
		return;
	}

	int clearTimeout = 2000;
	while ((SIO_STAT(0) & SIO_STAT_RX_NOT_EMPTY) && clearTimeout-- > 0)
		SIO_DATA(0);

	uint8_t response[8] = {0, 0, 0, 0, 0x80, 0x80, 0x80, 0x80};
	uint8_t request[] = { 0x42, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

	response[0] = exchangeByteWithTimeout(request[0], 20000);
	if (!waitForAcknowledge(500)) goto done;

	int type = response[0] >> 4;
	int halfwords = response[0] & 0x0F;
	int responseLen = (halfwords + 1) * 2;
	if (responseLen > 8) responseLen = 8;

	for (int i = 1; i < responseLen; i++) {
		response[i] = exchangeByteWithTimeout(request[i], 20000);
		if (i < responseLen - 1 && !waitForAcknowledge(500))
			break;
	}

	state->buttons = (response[2] | (response[3] << 8)) ^ 0xFFFF;

	if (type == 0x7 || type == 0x5) {
		state->isAnalog = true;
		state->rightX = response[4];
		state->rightY = response[5];
		state->leftX = response[6];
		state->leftY = response[7];
	}

done:
	delayMicroseconds(60);
	SIO_CTRL(0) &= ~SIO_CTRL_DTR;
}
