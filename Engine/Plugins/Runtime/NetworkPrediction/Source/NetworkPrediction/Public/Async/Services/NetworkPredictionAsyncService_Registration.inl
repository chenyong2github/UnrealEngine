// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "NetworkPredictionUtil.h"
#include "Algo/Find.h"
#include "Algo/BinarySearch.h"

namespace UE_NP {

class IAsyncRegistrationService
{
public:

	virtual ~IAsyncRegistrationService() = default;
	virtual void UnregisterInstance(FNetworkPredictionAsyncID ID, FNetworkPredictionSimCallbackInput* Input) = 0;
	virtual void RemapClientInstance(FNetworkPredictionAsyncID ClientID, FNetworkPredictionAsyncID ServerID, FNetworkPredictionSimCallbackInput* Input) = 0;
	
	virtual void SetInputSource(FNetworkPredictionAsyncID ID, ENetworkPredictionAsyncInputSource Type, bool bIsLocal) = 0;
};

template<typename AsyncModelDef>
class TAsyncRegistrationService : public IAsyncRegistrationService
{
	HOIST_ASYNCMODELDEF_TYPES()

public:

	TAsyncRegistrationService(TAsyncModelDataStore_External<AsyncModelDef>* InDataStore)
	{
		npCheck(InDataStore != nullptr);
		DataStore = InDataStore;
	}

	void RegisterInstance(FNetworkPredictionAsyncID ID, FNetworkPredictionSimCallbackInput* Input, LocalStateType&& InitialValueLocal, NetStateType&& InitialValueNet, InputCmdType* PendingInputCmd, NetStateType* OutNetState)
	{
		if (ID.IsClientGenerated())
		{
			DeferredInitMap.Emplace(ID, FDeferredRegistration{Input, MoveTemp(InitialValueLocal), MoveTemp(InitialValueNet), PendingInputCmd, OutNetState});
		}
		else
		{
			RegisterInstance_Impl(ID, Input, MoveTemp(InitialValueLocal), MoveTemp(InitialValueNet), PendingInputCmd, OutNetState);
		}
	}

	void UnregisterInstance( FNetworkPredictionAsyncID ID, FNetworkPredictionSimCallbackInput* Input) final override
	{
		if (ID.IsClientGenerated())
		{
			DeferredInitMap.Remove(ID);
			DeferredInputSource.Remove(ID);
		}
		else if(ID.IsValid())
		{
			TAsncInstanceStaticData<AsyncModelDef> InstanceData = DataStore->Instances.FindAndRemoveChecked(ID);
			DataStore->FreeIndices[InstanceData.Index] = true;

			// Remove from active input data structure
			ClearInputSource(ID, InstanceData.Index);

			// also remove from the inactive struct
			int32 idx = DataStore->InActivePendingInputCmds.IndexOfByKey(InstanceData.Index);
			if (idx != INDEX_NONE)
			{
				DataStore->InActivePendingInputCmds.RemoveAtSwap(idx, 1, false);
			}

			TAsyncModelDataStore_Input<AsyncModelDef>* InputDataStore = Input->GetDataStore<AsyncModelDef>();
			InputDataStore->DeletedInstances.Add(ID);
		}
	}

	void SetInputSource(FNetworkPredictionAsyncID ID, ENetworkPredictionAsyncInputSource Type, bool bIsLocal) final override
	{
		if (ID.IsClientGenerated())
		{
			DeferredInputSource.Emplace(ID, FDeferredInputSource{Type, bIsLocal});
		}
		else
		{
			SetInputSource_Impl(ID, Type, bIsLocal);
		}
	}

	void RemapClientInstance(FNetworkPredictionAsyncID ClientID, FNetworkPredictionAsyncID ServerID, FNetworkPredictionSimCallbackInput* AsyncInput) final override
	{
		// Complete deferred registration on ServerID
		FDeferredRegistration DeferredInfo = DeferredInitMap.FindAndRemoveChecked(ClientID);
		RegisterInstance_Impl(ServerID, DeferredInfo.Input, MoveTemp(DeferredInfo.InitialValueLocal), MoveTemp(DeferredInfo.InitialValueNet), DeferredInfo.PendingInputCmd, DeferredInfo.OutNetState);

		// Remap deferred InputSource data
		if (FDeferredInputSource* InputSource = DeferredInputSource.Find(ClientID))
		{
			SetInputSource_Impl(ServerID, InputSource->Type, InputSource->bIsLocal);
		}

		// Remap any deferred local state mods
		TAsyncModelDataStore_Input<AsyncModelDef>* InputData = AsyncInput->GetDataStore<AsyncModelDef>();
		npCheckSlow(InputData);

		// note: going in reverse would be better since we could use RemoveAtSwap but preserving order that mods are applied is important and that would reverse them
		for (int32 idx=0; idx < DataStore->DeferredLocalStateMods.Num(); ++idx)
		{
			TAsyncLocalStateMod<AsyncModelDef>& StateMod = DataStore->DeferredLocalStateMods[idx];
			if (StateMod.ID == ClientID)
			{
				InputData->LocalStateMods.Emplace(ServerID, MoveTemp(StateMod.Func));
				DataStore->DeferredLocalStateMods.RemoveAt(idx, 1, false);
			}
		}
	}

private:
	
	void RegisterInstance_Impl(FNetworkPredictionAsyncID ID, FNetworkPredictionSimCallbackInput* Input, LocalStateType&& InitialValueLocal, NetStateType&& InitialValueNet, InputCmdType* PendingInputCmd, NetStateType* OutNetState)
	{
		npCheckSlow(ID.IsClientGenerated() == false);

		// Pick a local Index for this instance
		int32 FreeIndice = DataStore->FreeIndices.Find(false);
		if (FreeIndice == INDEX_NONE)
		{
			FreeIndice = DataStore->FreeIndices.Num();
			NpResizeBitArray(DataStore->FreeIndices, DataStore->FreeIndices.Num() + 32);
			npCheckSlow(DataStore->FreeIndices[FreeIndice] == false);
		}

		DataStore->FreeIndices[FreeIndice] = true;

		// Update external data store
		TAsncInstanceStaticData<AsyncModelDef>& InstanceData = DataStore->Instances.Add(ID);		
		InstanceData.Index = FreeIndice;
		InstanceData.LocalState = MoveTemp(InitialValueLocal);

		if (PendingInputCmd)
		{
			DataStore->InActivePendingInputCmds.Emplace(TAsyncPendingInputCmdPtr<AsyncModelDef>{FreeIndice, PendingInputCmd});
		}

		if (OutNetState)
		{
			NpResizeForIndex(DataStore->OutputWriteTargets, FreeIndice);
			DataStore->OutputWriteTargets[FreeIndice] = {OutNetState};
			*OutNetState = InitialValueNet;
		}

		// Cheat a little and put the initial state values into the latest snapshot so we don't have to special case this in other spots
		NpResizeForIndex(DataStore->LatestSnapshot.NetStates, FreeIndice);
		DataStore->LatestSnapshot.NetStates[FreeIndice] = InitialValueNet;
		NpResizeForIndex(DataStore->LatestSnapshot.InputCmds, FreeIndice);

		// Marshal via Input
		TAsyncModelDataStore_Input<AsyncModelDef>* InputDataStore = Input->GetDataStore<AsyncModelDef>();
		npCheckSlow(InputDataStore);

		InputDataStore->NewInstances.Add({ID, InstanceData, MoveTemp(InitialValueNet)});
	}

	void SetInputSource_Impl(FNetworkPredictionAsyncID ID, ENetworkPredictionAsyncInputSource Type, bool bIsLocal)
	{
		npCheckSlow(ID.IsClientGenerated() == false);

		const int32 InstanceIndex = DataStore->Instances.FindChecked(ID).Index;

		// Remove previous entries
		ClearInputSource(ID, InstanceIndex);

		// Add this ID to the one of the lists
		switch(Type)
		{
			case ENetworkPredictionAsyncInputSource::None:
			{
				break;
			}

			case ENetworkPredictionAsyncInputSource::Buffered:
			{
				TAsyncPendingInputCmdBuffer<AsyncModelDef>& PendingInputs = DataStore->PendingInputCmdBuffers.Add(ID).Get();
				PendingInputs.Index = InstanceIndex;
				break;
			}

			case ENetworkPredictionAsyncInputSource::Local:
			{
				int32 InactiveIdx = DataStore->InActivePendingInputCmds.IndexOfByKey(InstanceIndex);
				if (ensureMsgf(InactiveIdx != INDEX_NONE, TEXT("Could not find PendingInputCmd info for %d"), (int32)ID))
				{
					DataStore->ActivePendingInputCmds.Add(DataStore->InActivePendingInputCmds[InactiveIdx]);
				}
				break;
			}
		}
	}

	void ClearInputSource(FNetworkPredictionAsyncID ID, const int32 InstanceIndex)
	{
		int32 idx = DataStore->ActivePendingInputCmds.IndexOfByKey(InstanceIndex);
		if (idx != INDEX_NONE)
		{
			DataStore->ActivePendingInputCmds.RemoveAtSwap(idx, 1, false);
		}
		DataStore->PendingInputCmdBuffers.Remove(ID);
	}

	struct FDeferredRegistration
	{
		FNetworkPredictionSimCallbackInput* Input;
		LocalStateType InitialValueLocal;
		NetStateType InitialValueNet;
		InputCmdType* PendingInputCmd;
		NetStateType* OutNetState;
	};

	TSortedMap<FNetworkPredictionAsyncID, FDeferredRegistration> DeferredInitMap;

	struct FDeferredInputSource
	{
		ENetworkPredictionAsyncInputSource Type = ENetworkPredictionAsyncInputSource::None;
		bool bIsLocal = false;
	};

	TSortedMap<FNetworkPredictionAsyncID, FDeferredInputSource> DeferredInputSource;

	TAsyncModelDataStore_External<AsyncModelDef>* DataStore = nullptr;
};

} // namespace UE_NP