#pragma once
#include "CoreMinimal.h"

class RENDERCORE_API ICustomResourcePool
{
public:
	virtual ~ICustomResourcePool() {}
	virtual void Tick() = 0;

	static void TickPoolElements();
};
extern RENDERCORE_API ICustomResourcePool* GCustomResourcePool;
