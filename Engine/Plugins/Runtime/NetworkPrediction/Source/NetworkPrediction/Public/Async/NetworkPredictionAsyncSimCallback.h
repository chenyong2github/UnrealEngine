// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Physics/Experimental/PhysScene_Chaos.h"
#include "NetworkPredictionAsyncInstanceData.h"

namespace UE_NP {

struct FNetworkPredictionSimCallbackInput : public Chaos::FSimCallbackInput
{
	bool NetSendInputCmd(FNetBitWriter& Ar) override { return true; } // FIXME: remove these functions	
	bool NetRecvInputCmd(APlayerController* PC, FNetBitReader& Ar) override { return true; } // FIXME: remove these functions

	void Reset()
	{
		DataStoreCollection.Reset();
	}

	template<typename AsyncModelDef>
	TAsyncModelDataStore_Input<AsyncModelDef>* GetDataStore()
	{
		return DataStoreCollection.GetDataStore<TAsyncModelDataStore_Input<AsyncModelDef>, AsyncModelDef>();
	}

	template<typename AsyncModelDef>
	const TAsyncModelDataStore_Input<AsyncModelDef>* GetDataStore() const
	{
		return DataStoreCollection.GetDataStore<TAsyncModelDataStore_Input<AsyncModelDef>, AsyncModelDef>();
	}

private:

	FAsyncDataStoreCollection DataStoreCollection;
};


struct FNetworkPredictionSimCallbackOutput : public Chaos::FSimCallbackOutput
{
	void Reset()
	{
		SimulationFrame = 0;
		LocalFrameOffset = 0;
	}

	int32 SimulationFrame = 0;
	int32 LocalFrameOffset = 0;

	template<typename AsyncModelDef>
	TAsyncModelDataStore_Output<AsyncModelDef>* GetDataStore()
	{
		return DataStoreCollection.GetDataStore<TAsyncModelDataStore_Output<AsyncModelDef>, AsyncModelDef>();
	}

	FAsyncDataStoreCollection DataStoreCollection;
};

} // namespace UE_NP