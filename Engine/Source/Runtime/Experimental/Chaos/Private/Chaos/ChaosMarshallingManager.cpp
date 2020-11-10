// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/ChaosMarshallingManager.h"

namespace Chaos
{

int32 SimDelay = 0;
FAutoConsoleVariableRef CVarSimDelay(TEXT("p.simDelay"),SimDelay,TEXT(""));

FChaosMarshallingManager::FChaosMarshallingManager()
: ExternalTime(0)
, ExternalTimestamp(0)
, SimTime(0)
, InternalTimestamp(-1)
, ProducerData(nullptr)
, Delay(SimDelay)
{
	PrepareExternalQueue();
}

void FChaosMarshallingManager::PrepareExternalQueue()
{
	if(!PushDataPool.Dequeue(ProducerData))
	{
		BackingBuffer.Add(MakeUnique<FPushPhysicsData>());
		ProducerData = BackingBuffer.Last().Get();
	}

	ProducerData->StartTime = ExternalTime;
}

void FChaosMarshallingManager::Step_External(FReal ExternalDT)
{
	for(FSimCallbackDataPair& Pair : ProducerData->SimCallbackDataPairs)
	{
		Pair.Callback->LatestCallbackData = nullptr;	//mark data as marshalled, any new data must be in a new data packet
	}

	//stored in reverse order for easy removal later. Might want to use a circular buffer if perf is bad here
	//expecting queue to be fairly small (3,4 at most) so probably doesn't matter
	ExternalQueue.Insert(ProducerData,0);

	ExternalTime += ExternalDT;
	++ExternalTimestamp;
	PrepareExternalQueue();
}

TArray<FPushPhysicsData*> FChaosMarshallingManager::StepInternalTime_External(FReal InternalDt)
{
	TArray<FPushPhysicsData*> PushDataUpToEnd;

	if(Delay == 0)
	{
		SimTime += InternalDt;

		//if dt is exactly 0 we still want to get first data so add an epsilon. todo: is there a better way to handle this?
		const FReal EndTime = InternalDt == 0 ? SimTime + KINDA_SMALL_NUMBER : SimTime;

		//stored in reverse order so push from back to front
		for(int32 Idx = ExternalQueue.Num() - 1; Idx >= 0; --Idx)
		{
			if(ExternalQueue[Idx]->StartTime < EndTime)
			{
				FPushPhysicsData* PushData = ExternalQueue.Pop(/*bAllowShrinking=*/false);
				PushDataUpToEnd.Add(PushData);
				++InternalTimestamp;
			}
			else
			{
				//sorted so stop
				break;
			}
		}
	}
	else
	{
		--Delay;
	}

	return PushDataUpToEnd;
}

void FChaosMarshallingManager::FreeData_Internal(FPushPhysicsData* PushData)
{
	PushData->Reset();
	PushDataPool.Enqueue(PushData);
}

void FChaosMarshallingManager::FreeCallbackData_Internal(FSimCallbackHandlePT* Callback)
{
	if(Callback->IntervalData.Num())
	{
		if(Callback->Handle->FreeExternal)
		{
			Callback->Handle->FreeExternal(Callback->IntervalData);	//any external data should be freed
		}

		for(FSimCallbackData* Data : Callback->IntervalData)
		{
			CallbackDataPool.Enqueue(Data);
		}

		Callback->IntervalData.Reset();
	}
}

void FPushPhysicsData::Reset()
{
	DirtyProxiesDataBuffer.Reset();
	SimCallbacksToAdd.Reset();
	SimCallbacksToRemove.Reset();
	SimCallbackDataPairs.Reset();
}

}