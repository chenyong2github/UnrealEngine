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
, ProducerData(nullptr)
, CurPullData(nullptr)
, Delay(SimDelay)
{
	PrepareExternalQueue_External();
	PreparePullData();
}

FChaosMarshallingManager::~FChaosMarshallingManager() = default;

void FChaosMarshallingManager::FinalizePullData_Internal(int32 LastExternalTimestampConsumed, float SimStartTime, float DeltaTime)
{
	CurPullData->SolverTimestamp = LastExternalTimestampConsumed;
	CurPullData->ExternalStartTime = SimStartTime;
	CurPullData->ExternalEndTime = SimStartTime + DeltaTime;
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

void FChaosMarshallingManager::Step_External(FReal ExternalDT, const int32 NumSteps)
{
	ensure(NumSteps > 0);

	FPushPhysicsData* FirstStepData = nullptr;
	for(int32 Step = 0; Step < NumSteps; ++Step)
	{
		for (FSimCallbackInputAndObject& Pair : ProducerData->SimCallbackInputs)
		{
			Pair.CallbackObject->CurrentExternalInput_External = nullptr;	//mark data as marshalled, any new data must be in a new data packet
			Pair.Input->SetNumSteps_External(NumSteps);
		}

		//stored in reverse order for easy removal later. Might want to use a circular buffer if perf is bad here
		//expecting queue to be fairly small (3,4 at most) so probably doesn't matter
		ProducerData->ExternalDt = ExternalDT;
		ProducerData->ExternalTimestamp = ExternalTimestamp_External;
		ProducerData->IntervalStep = Step;
		ProducerData->IntervalNumSteps = NumSteps;

		ExternalQueue.Insert(ProducerData, 0);

		if(Step == 0)
		{
			FirstStepData = ProducerData;
		}
		else
		{
			//copy sub-step only data
			ProducerData->CopySubstepData(*FirstStepData);
		}

		ExternalTime_External += ExternalDT;
		PrepareExternalQueue_External();
	}

	++ExternalTimestamp_External;
}

FPushPhysicsData* FChaosMarshallingManager::StepInternalTime_External()
{
	if (Delay == 0)
	{
		if(ExternalQueue.Num())
		{
			return ExternalQueue.Pop(/*bAllowShrinking=*/false);
		}
	}
	else
	{
		--Delay;
	}

	return nullptr;
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

void FPushPhysicsData::CopySubstepData(const FPushPhysicsData& FirstStepData)
{
	const FDirtyPropertiesManager& FirstManager = FirstStepData.DirtyPropertiesManager;
	DynamicsWeight = FirstStepData.DynamicsWeight;
	DirtyPropertiesManager.SetNumParticles(FirstStepData.DirtyProxiesDataBuffer.NumDirtyProxies());
	FirstStepData.DirtyProxiesDataBuffer.ForEachProxy([this, &FirstManager](int32 FirstDataIdx, const FDirtyProxy& Dirty)
		{
			switch (Dirty.Proxy->GetType())
			{
			case EPhysicsProxyType::SingleRigidParticleType:
			{
				if (const FParticleDynamics* DynamicsData = Dirty.ParticleData.FindDynamics(FirstManager, FirstDataIdx))
				{
					DirtyProxiesDataBuffer.Add(Dirty.Proxy);
					FParticleDynamics& SubsteppedDynamics = DirtyPropertiesManager.GetParticlePool<FParticleDynamics, EParticleProperty::Dynamics>().GetElement(Dirty.Proxy->GetDirtyIdx());
					SubsteppedDynamics = *DynamicsData;
					//we don't want to sub-step impulses so those are cleared in the sub-step
					SubsteppedDynamics.SetAngularImpulse(FVec3(0));
					SubsteppedDynamics.SetLinearImpulse(FVec3(0));
					FDirtyProxy& NewDirtyProxy = DirtyProxiesDataBuffer.GetDirtyProxyAt(Dirty.Proxy->GetDirtyIdx());
					NewDirtyProxy.ParticleData.DirtyFlag(EParticleFlags::Dynamics);
				}

				Dirty.Proxy->ResetDirtyIdx();	//dirty idx is only used temporarily
				break;
			}
			default: { break; }
		}
	});

	//make sure inputs are available to every sub-step
	SimCallbackInputs = FirstStepData.SimCallbackInputs;
}

FSimCallbackInput* ISimCallbackObject::GetProducerInputData_External()
{
	if (CurrentExternalInput_External == nullptr)
	{
		FChaosMarshallingManager& Manager = Solver->GetMarshallingManager();
		CurrentExternalInput_External = AllocateInputData_External();
		Manager.AddSimCallbackInputData_External(this, CurrentExternalInput_External);
	}

	return CurrentExternalInput_External;
}

}