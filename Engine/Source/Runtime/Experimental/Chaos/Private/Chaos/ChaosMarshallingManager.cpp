// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/ChaosMarshallingManager.h"
#include "Chaos/PullPhysicsDataImp.h"

namespace Chaos
{

int32 SimDelay = 0;
FAutoConsoleVariableRef CVarSimDelay(TEXT("p.simDelay"),SimDelay,TEXT(""));

FChaosMarshallingManager::FChaosMarshallingManager()
: ExternalTime_External(0)
, ExternalTimestamp_External(0)
, SimTime_External(0)
, InternalTimestamp_External(-1)
, ProducerData(nullptr)
, CurPullData(nullptr)
, Delay(SimDelay)
{
	PrepareExternalQueue_External();
	PreparePullData();
}

FChaosMarshallingManager::~FChaosMarshallingManager() = default;

void FChaosMarshallingManager::FinalizePullData_Internal(int32 LastExternalTimestampConsumed)
{
	CurPullData->SolverTimestamp = LastExternalTimestampConsumed;
	PullDataQueue.Enqueue(CurPullData);
	PreparePullData();
}

void FChaosMarshallingManager::PreparePullData()
{
	if(!PullDataPool.Dequeue(CurPullData))
	{
		const int32 Idx = BackingPullBuffer.Add(MakeUnique<FPullPhysicsData>());
		CurPullData = BackingPullBuffer[Idx].Get();
	}
}

void FChaosMarshallingManager::PrepareExternalQueue_External()
{
	if(!PushDataPool.Dequeue(ProducerData))
	{
		BackingBuffer.Add(MakeUnique<FPushPhysicsData>());
		ProducerData = BackingBuffer.Last().Get();
	}

	ProducerData->StartTime = ExternalTime_External;
}

void FChaosMarshallingManager::Step_External(FReal ExternalDT)
{
	for (FSimCallbackInputAndObject& Pair : ProducerData->SimCallbackInputs)
	{
		Pair.CallbackObject->CurrentExternalInput_External = nullptr;	//mark data as marshalled, any new data must be in a new data packet
	}

	//stored in reverse order for easy removal later. Might want to use a circular buffer if perf is bad here
	//expecting queue to be fairly small (3,4 at most) so probably doesn't matter
	ExternalQueue.Insert(ProducerData,0);

	ExternalTime_External += ExternalDT;
	++ExternalTimestamp_External;
	PrepareExternalQueue_External();
}

TArray<FPushPhysicsData*> FChaosMarshallingManager::StepInternalTime_External(FReal InternalDt)
{
	TArray<FPushPhysicsData*> PushDataUpToEnd;

	if(Delay == 0)
	{
		SimTime_External += InternalDt;

		//if dt is exactly 0 we still want to get first data so add an epsilon. todo: is there a better way to handle this?
		const FReal EndTime = InternalDt == 0 ? SimTime_External + KINDA_SMALL_NUMBER : SimTime_External;

		//stored in reverse order so push from back to front
		for(int32 Idx = ExternalQueue.Num() - 1; Idx >= 0; --Idx)
		{
			if(ExternalQueue[Idx]->StartTime < EndTime)
			{
				FPushPhysicsData* PushData = ExternalQueue.Pop(/*bAllowShrinking=*/false);
				PushDataUpToEnd.Add(PushData);
				++InternalTimestamp_External;
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

void FChaosMarshallingManager::FreePullData_External(FPullPhysicsData* PullData)
{
	PullData->Reset();
	PullDataPool.Enqueue(PullData);
}

void FPushPhysicsData::Reset()
{
	DirtyProxiesDataBuffer.Reset();

	SimCallbackObjectsToAdd.Reset();
	SimCallbackObjectsToRemove.Reset();
	SimCallbackInputs.Reset();
}

FSimCallbackInput* ISimCallbackObject::GetProducerInputData_External()
{
	if (CurrentExternalInput_External == nullptr)
	{
		FChaosMarshallingManager& Manager = Solver->GetMarshallingManager();
		CurrentExternalInput_External = AllocateInputData_External();
		CurrentExternalInput_External->ExternalTime = Manager.GetExternalTime_External();
		Manager.AddSimCallbackInputData_External(this, CurrentExternalInput_External);
	}

	return CurrentExternalInput_External;
}

}