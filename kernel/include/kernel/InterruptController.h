#pragma once

#include <stdint.h>

class InterruptController
{
public:
	virtual ~InterruptController() {}

	virtual void EOI(uint8_t) = 0;
	virtual void EnableIrq(uint8_t) = 0;
	virtual void GetISR(uint32_t[8]) = 0;

	static void Initialize(bool force_pic);
	static InterruptController& Get();
};