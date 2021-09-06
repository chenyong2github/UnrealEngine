// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "NetworkPredictionAsyncDataStore.h"
#include "NetworkPredictionCheck.h"
#include "Async/NetworkPredictionAsyncInstanceData.h"

// Data that is available in sim ticks. This is meant to hold the complete "World view of what is going o
struct FNetworkPredictionAsyncTickContext
{
	FNetworkPredictionAsyncTickContext(UE_NP::FAsyncDataStoreCollection* InDataStoreCollection)
		: DataStoreCollection(InDataStoreCollection) { }

	UWorld* World = nullptr;
	float DeltaTimeSeconds;
	int32 SimulationFrame;
	int32 LocalStorageFrame;
	bool bIsResim = false;

	template<typename AsyncModelDef>
	UE_NP::TAsyncModelDataStore_Input<AsyncModelDef>* GetDataStore()
	{
		npCheckSlow(DataStoreCollection);
		return DataStoreCollection->GetDataStore<UE_NP::TAsyncModelDataStore_Output<AsyncModelDef>, AsyncModelDef>();
	}

private:

	UE_NP::FAsyncDataStoreCollection* DataStoreCollection;
};