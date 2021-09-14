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
			InputCmdType& InputCmd = Snapshot.InputCmds[InstanceData.Index];
			NetStateType& NetState = Snapshot.NetStates[InstanceData.Index];

			SimulationTickType::Tick_Internal(Context, ID, InputCmd, InstanceData.LocalState, NetState);
		}
	}

private:

	TAsyncModelDataStore_Internal<AsyncModelDef>* DataStore = nullptr;
};


} // namespace UE_NP