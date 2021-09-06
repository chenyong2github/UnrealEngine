// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "UObject/CoreNet.h"

namespace UE_NP {

class INetSerializeService
{
public:
	virtual ~INetSerializeService() = default;

	// Receiving Networked data. Note that frame here = "The Frame this data was sent on"
	virtual void NetRecv(FNetworkPredictionAsyncID ID, uint8 Flags, int32 ThisFrame, int32 FutureFrame, FNetBitReader& Ar) = 0;
	virtual void NetSend(FNetworkPredictionAsyncID ID, uint8 Flags, int32 ThisFrame, int32 PendingInputCmdFrame, FNetBitWriter& Ar) = 0;
	virtual void NetSerializeInputCmd(FNetworkPredictionAsyncID ID, int32 Frame, FArchive& Ar) = 0;
};

template<typename AsyncModelDef>
class TNetSerializeService : public INetSerializeService
{
	HOIST_ASYNCMODELDEF_TYPES()

public:

	TNetSerializeService(TAsyncModelDataStore_External<AsyncModelDef>* InDataStore)
	{
		npCheck(InDataStore != nullptr);
		DataStore = InDataStore;
	}

	void NetRecv(FNetworkPredictionAsyncID ID, uint8 Flags, int32 ThisFrame, int32 FutureFrame, FNetBitReader& Ar) final override
	{
		TAsncInstanceStaticData<AsyncModelDef>& InstanceData = DataStore->Instances.FindChecked(ID);

		DataStore->PendingNetRecv.MarkIndexDirty(InstanceData.Index);

		typename TAsyncNetRecvData<AsyncModelDef>::FInstance& NetRecvData = DataStore->PendingNetRecv.NetRecvInstances[InstanceData.Index];
		NetRecvData.Frame = ThisFrame;
		NetRecvData.Flags = Flags;
		NetRecvData.InputCmd.NetSerialize(Ar);
		NetRecvData.NetState.NetSerialize(Ar);
		NetRecvData.LatestInputCmd.NetSerialize(Ar);
	}

	void NetSend(FNetworkPredictionAsyncID ID, uint8 Flags, int32 ThisFrame, int32 PendingInputCmdFrame, FNetBitWriter& Ar) final override
	{
		TAsncInstanceStaticData<AsyncModelDef>& InstanceData = DataStore->Instances.FindChecked(ID);

		const int32 idx = InstanceData.Index;
		npCheckSlow(DataStore->LatestSnapshot.InputCmds.IsValidIndex(idx));
		npCheckSlow(DataStore->LatestSnapshot.NetStates.IsValidIndex(idx));

		DataStore->LatestSnapshot.InputCmds[idx].NetSerialize(Ar);
		DataStore->LatestSnapshot.NetStates[idx].NetSerialize(Ar);

		// Serialize "Future Input" / LatestInputCmd
		NetSerializeInputCmd_Impl(ID, PendingInputCmdFrame, Ar, idx);
	}

	void NetSerializeInputCmd(FNetworkPredictionAsyncID ID, int32 PendingInputCmdFrame, FArchive& Ar) final override
	{
		NetSerializeInputCmd_Impl(ID, PendingInputCmdFrame, Ar, INDEX_NONE);
	}

private:

	void NetSerializeInputCmd_Impl(FNetworkPredictionAsyncID ID, int32 PendingInputCmdFrame, FArchive& Ar, int32 idx)
	{
		//	This input is either in a PendingInputCmdBuffers or we just need to pull it from the PendingInputCmd*
		if (PendingInputCmdFrame != INDEX_NONE)
		{
			if (auto* FoundPtr = DataStore->PendingInputCmdBuffers.Find(ID))
			{
				// Need latest 
				TAsyncPendingInputCmdBuffer<AsyncModelDef>& PendingInputs = FoundPtr->Get();
				PendingInputs.Buffer[PendingInputCmdFrame % PendingInputs.Buffer.Num()].NetSerialize(Ar);
			}
			else
			{
				UE_LOG(LogNetworkPrediction, Warning, TEXT("%s Could not find PendingInputCmdBuffer for Instance %d"), AsyncModelDef::GetName(), (int32)ID);
				InputCmdType Default;
				Default.NetSerialize(Ar);
			}
		}
		else
		{
			npEnsure(Ar.IsSaving()); // we should only be here in the writing phase. Saved InputCmds should always end up in the PendingInputCmdBuffer
			if (TAsyncPendingInputCmdPtr<AsyncModelDef>* Ptr = DataStore->ActivePendingInputCmds.FindByKey(idx != INDEX_NONE ? idx : DataStore->Instances.FindChecked(ID).Index))
			{
				npCheckSlow(Ptr->PendingInputCmd);
				Ptr->PendingInputCmd->NetSerialize(Ar);
			}
			else
			{
				UE_LOG(LogNetworkPrediction, Warning, TEXT("%s Could not find ActivePendingInputCmds for Instance %d"), AsyncModelDef::GetName(), (int32)ID);
				InputCmdType Default;
				Default.NetSerialize(Ar);
			}
		}
	}

	TAsyncModelDataStore_External<AsyncModelDef>* DataStore = nullptr;
};


} // namespace UE_NP