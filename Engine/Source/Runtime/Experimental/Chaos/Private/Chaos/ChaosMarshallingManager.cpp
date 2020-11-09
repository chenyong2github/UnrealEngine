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
	ProducerData->ExternalDt = ExternalDT;
	ExternalQueue.Insert(ProducerData,0);

	ExternalTime_External += ExternalDT;
	++ExternalTimestamp_External;
	PrepareExternalQueue_External();
}

TArray<FPushPhysicsData*> FChaosMarshallingManager::StepInternalTime_External(FReal InternalDt, bool bUseAsync)
{
	TArray<FPushPhysicsData*> PushDataUpToEnd;

	if(Delay == 0)
	{
		const FReal EndTime = SimTime_External + InternalDt;

		//stored in reverse order so push from back to front

		//see if we have enough inputs to proceed
		int32 LatestIdx = INDEX_NONE;
		for (int32 Idx = ExternalQueue.Num() - 1; Idx >= 0; --Idx)
		{
			if (ExternalQueue[Idx]->StartTime + ExternalQueue[Idx]->ExternalDt >= EndTime || !bUseAsync)	//in sync mode we always consume first input
			{
				LatestIdx = Idx;
				break;
			}
		}

		//if we do, consume them all and reorder
		if(LatestIdx != INDEX_NONE)
		{
			for (int32 Idx = ExternalQueue.Num() - 1; Idx >= LatestIdx; --Idx)
			{
				FPushPhysicsData* PushData = ExternalQueue.Pop(/*bAllowShrinking=*/false);
				PushDataUpToEnd.Add(PushData);
				++InternalTimestamp_External;
			}

			//consumed inputs for interval of InternalDt, so advance the sim time by that amount
			SimTime_External += InternalDt;
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