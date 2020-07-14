// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/ChaosMarshallingManager.h"

namespace Chaos
{

FChaosMarshallingManager::FChaosMarshallingManager()
: bFixedDt(false)
, SimStep(0)
, ExternalTime(0)
, SimTime(0)
{
	PrepareExternalQueue();
}

void FChaosMarshallingManager::PrepareExternalQueue()
{
	FPushPhysicsData* NewData;
	if(!PushDataPool.Dequeue(NewData))
	{
		BackingBuffer.Add(MakeUnique<FPushPhysicsData>());
		NewData = BackingBuffer.Last().Get();
	}

	NewData->StartTime = ExternalTime;
	ExternalQueue.Add(NewData);
}

void FChaosMarshallingManager::Step_External(FReal ExternalDT)
{
	//todo: handle dt bucketing etc...
	for(int32 Idx = ExternalQueue.Num() - 1; Idx >= 0; --Idx)
	{
		FPushPhysicsData* PushData = ExternalQueue[Idx];
		PushData->ExternalDt = ExternalDT;
		InternalQueue.Enqueue(PushData);
	}
	ExternalQueue.Reset();

	//do we care about precision here?
	ExternalTime += ExternalDT;

	PrepareExternalQueue();
}

FPushPhysicsData* FChaosMarshallingManager::ConsumeData_Internal(FReal StartTime, FReal InternalDt)
{
	if(StartTime < 0)	//TODO: get rid of this
	{
		FPushPhysicsData* OutData;
		ensure(InternalQueue.Dequeue(OutData));	//if user is calling with negative time, they expect to get something back
		return OutData;
	}
	FPushPhysicsData** PushData = InternalQueue.Peek();
	if(PushData)
	{
		//todo: handle dt overlaps
		if((*PushData)->StartTime <= StartTime)
		{
			FPushPhysicsData* OutData;
			ensure(InternalQueue.Dequeue(OutData));	//we already know there's an entry at head, and we're the only thread dequeuing
			return OutData;
		}
	}

	return nullptr;
}

void FChaosMarshallingManager::FreeData_Internal(FPushPhysicsData* PushData)
{
	PushData->DirtyProxiesDataBuffer.Reset();
	PushDataPool.Enqueue(PushData);
}

}