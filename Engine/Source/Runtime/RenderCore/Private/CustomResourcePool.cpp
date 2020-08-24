// Copyright Epic Games, Inc. All Rights Reserved.

#include "CustomResourcePool.h"

ICustomResourcePool* GCustomResourcePool = nullptr;

void ICustomResourcePool::TickPoolElements()
{
	if (GCustomResourcePool)
	{
		GCustomResourcePool->Tick();
	}
}
