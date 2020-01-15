// Copyright Epic Games, Inc. All Rights Reserved.
#include "Chaos/DebugDrawQueue.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ScopeLock.h"

#if CHAOS_DEBUG_DRAW
using namespace Chaos;

int32 FDebugDrawQueue::EnableDebugDrawing = 0;

int32 FDebugDrawQueue::NumConsumers = 0;

FAutoConsoleVariableRef CVarEnableDebugDrawingChaos = FDebugDrawQueue::MakeCVarRef();

void FDebugDrawQueue::SetConsumerActive(void* Consumer, bool bConsumerActive)
{
	FScopeLock Lock(&ConsumersCS);

	if (bConsumerActive)
	{
		Consumers.AddUnique(Consumer);
	}
	else
	{
		Consumers.Remove(Consumer);
	}

	NumConsumers = Consumers.Num();
}


FAutoConsoleVariableRef FDebugDrawQueue::MakeCVarRef()
{
	return FAutoConsoleVariableRef(TEXT("p.Chaos.DebugDrawing"), EnableDebugDrawing, TEXT("Whether to debug draw low level physics solver information"));
}

#endif
