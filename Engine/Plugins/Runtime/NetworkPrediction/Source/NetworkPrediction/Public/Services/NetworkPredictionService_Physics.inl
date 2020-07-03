// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "NetworkPredictionSimulation.h"
#include "NetworkPredictionConfig.h"
#include "Chaos/Particles.h"
#include "Chaos/ParticleHandle.h"

// The PhysicsService does bookkeeping for physics objects
//	-Tracing
//	-Updates PendingFrame for physics-only sims (no backing NetworkPrediction simulation)
class IPhysicsService
{
public:

	virtual ~IPhysicsService() = default;
	virtual void PostNetworkPredictionFrame(const FFixedTickState* TickState) = 0;
	virtual void PostPhysics() = 0; // todo
};

template<typename InModelDef>
class TPhysicsService : public IPhysicsService
{
public:

	using ModelDef = InModelDef;
	using PhysicsState = typename ModelDef::PhysicsState;

	enum { UpdatePendingFrame = !FNetworkPredictionDriver<ModelDef>::HasSimulation() };

	TPhysicsService(TModelDataStore<ModelDef>* InDataStore)
		: DataStore(InDataStore)
	{
		npCheckf(FNetworkPredictionDriver<ModelDef>::HasPhysics(), TEXT("TPhysicsService created for non physics having sim %s"), ModelDef::GetName());
	}

	void RegisterInstance(FNetworkPredictionID ID)
	{
		TInstanceData<ModelDef>* Instance = DataStore->Instances.Find(ID);
		npCheckSlow(Instance);

		Instances.Add((int32)ID, FInstance{ID.GetTraceID(), Instance->Info.Physics, Instance->Info.View});
	}

	void UnregisterInstance(FNetworkPredictionID ID)
	{
		Instances.Remove((int32)ID);
	}

	void PostNetworkPredictionFrame(const FFixedTickState* TickState) override final
	{
		npCheckSlow(TickState);

		if (UpdatePendingFrame)
		{
			// This is needed so that we can UE_NP_TRACE_SIM_TICK to tell the trace system the sim ticked this frame
			// Right now this is under the assumption that physics is fixed tick once per engine/physics frame.
			// When that changes, this will need to change. We will probably just want another function on IPhysicsService
			// that is called after every Np fixed tick / Physics ticked frame, however that neds up looking.
			// For now this gets us traced physics state which is super valuable.
			const int32 ServerFrame = (TickState->PendingFrame-1) + TickState->Offset; // -1 because PendingFrame was already ++ this frame
			UE_NP_TRACE_PUSH_TICK(ServerFrame * TickState->FixedStepMS, TickState->FixedStepMS, ServerFrame+1);
		}

		for (auto MapIt : Instances)
		{
			FInstance& Instance = MapIt.Value;
			
			// This is needed for FReplicationProxy::Identical to cause the rep proxy to replicate.
			// Could maybe have physics only sims use different rep proxies that can query the sleeping state themselves
			if (UpdatePendingFrame)
			{
				UE_NP_TRACE_SIM_TICK(Instance.TraceID);
				if (Instance.Handle->ObjectState() != Chaos::EObjectStateType::Sleeping)
				{
					Instance.View->PendingFrame = TickState->PendingFrame;
				}
			}

			UE_NP_TRACE_PHYSICS_STATE_CURRENT(ModelDef, Instance.Handle);
		}
	}

	void PostPhysics() override final
	{
		// Not implemented yet but the idea would be to do anything we need to do after physics run for the frame
		// Tracing is the obvious example though it complicates the general tracing system a bit since it adds
		// another point in the frame that we need to sample (pre NP tick (Net Recv/Rollback), post NP tick, post Physics tick)
	}
	
private:

	struct FInstance
	{
		int32 TraceID;
		FPhysicsActorHandle Handle;
		FNetworkPredictionStateView* View;
	};

	TSortedMap<int32, FInstance> Instances;
	TModelDataStore<ModelDef>* DataStore;
};