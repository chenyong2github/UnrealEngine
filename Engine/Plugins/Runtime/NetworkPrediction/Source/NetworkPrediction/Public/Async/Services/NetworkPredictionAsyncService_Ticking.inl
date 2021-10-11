// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace UE_NP {

class IAsyncTickingService
{
public:
	virtual ~IAsyncTickingService() = default;
	virtual void Tick_Internal(FNetworkPredictionAsyncTickContext& Context, FNetworkPredictionSimCallbackOutput& Output) = 0;
};

template<typename AsyncModelDef>
class TAsyncTickingService : public IAsyncTickingService
{
	HOIST_ASYNCMODELDEF_TYPES()

public:

	TAsyncTickingService(TAsyncModelDataStore_Internal<AsyncModelDef>* InDataStore)
	{
		npCheck(InDataStore != nullptr);
		DataStore = InDataStore;
	}

	void Tick_Internal(FNetworkPredictionAsyncTickContext& Context, FNetworkPredictionSimCallbackOutput& Output) final override
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(NPA_TickInternal);

		TAsncFrameSnapshot<AsyncModelDef>& Snapshot = DataStore->Frames[Context.LocalStorageFrame];

		for (auto& MapIt : DataStore->Instances)
		{
			FNetworkPredictionAsyncID& ID = MapIt.Key;
			TAsncInstanceStaticData<AsyncModelDef>& InstanceData = MapIt.Value;
			if (InstanceData.LocalSpawnFrame <= Context.LocalStorageFrame)			
			{
				if (Snapshot.InputCmds.Num() != Snapshot.NetStates.Num())
				{
					UE_LOG(LogNetworkPrediction, Error, TEXT("%s InputCmds and NetStates array size are out of sync (%d %d). LocalFrame: %d"), AsyncModelDef::GetName(), Snapshot.InputCmds.Num(), Snapshot.NetStates.Num(), Context.LocalStorageFrame);
				}


				if (Snapshot.InputCmds.IsValidIndex(InstanceData.Index) && Snapshot.NetStates.IsValidIndex(InstanceData.Index))
				{
					InputCmdType& InputCmd = Snapshot.InputCmds[InstanceData.Index];
					NetStateType& NetState = Snapshot.NetStates[InstanceData.Index];
					SimulationTickType::Tick_Internal(Context, ID, InputCmd, InstanceData.LocalState, NetState);
				}
				else
				{
					UE_LOG(LogNetworkPrediction, Warning, TEXT("%s Invalid Instanced %d @ index %d. LocalFrame: %d SpawnFrame: %d. Sizes: (%d %d)"), AsyncModelDef::GetName(), (int32)ID, InstanceData.Index, Context.LocalStorageFrame, InstanceData.LocalSpawnFrame, Snapshot.InputCmds.Num(), Snapshot.NetStates.Num());
				}
			}
		}
	}

private:

	TAsyncModelDataStore_Internal<AsyncModelDef>* DataStore = nullptr;
};


} // namespace UE_NP