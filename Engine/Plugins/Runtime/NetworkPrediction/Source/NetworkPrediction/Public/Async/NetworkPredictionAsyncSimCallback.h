// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Physics/Experimental/PhysScene_Chaos.h"
#include "NetworkPredictionAsyncInstanceData.h"

namespace UE_NP {

// This is the Chaos's Simcallback Input object for the NP system.
// It holds data that is marshaled GT -> PT each time a PhysicsStep task is kicked off.
// The data in here must be associated with an absolute (local) frame number.

// This is because of "substepping"
//	In this context, by substepping we mean multiple PhysicsStep tasks are kicked off in a single GT tick
//	(e.g, we hitched, had a GT delta time of 99ms and kicked off three PhysicsStep tasks at 33ms steps each)
//
//	When this happens, the Chaos::FSimCallbackInput is shared between all PT tasks. 
//	In other ords, the FSimCallbackInput contains all marshaled data for N physics steps. There is not a unique FSimCallbackInput per PhysicsStep when substepping.
//	(and this makes sense in the context of the GT. The GT has no "chance" to add data to the N+1 steps, since they are all kicked off at the same time).
//	But for Networking, we need to coordinate steps and InputCmds between clients and Server. Clients and servers will not substep "together". We need the behavior
//	of the simulation to be exactly the same whether we run 1 PhysicsStep at a time or whether we substep.
//
//	Therefore the data in here is stored in the FDataStoreInternal structure and keyed off the local frame.

struct FNetworkPredictionSimCallbackInput : public Chaos::FSimCallbackInput
{
	bool NetSendInputCmd(FNetBitWriter& Ar) override { return true; } // FIXME: remove these functions	
	bool NetRecvInputCmd(APlayerController* PC, FNetBitReader& Ar) override { return true; } // FIXME: remove these functions

	void Reset()
	{
		for (FDataStoreInternal& Item : DataStoreCollections)
		{
			Item.Frame = INDEX_NONE;
			Item.StoreCollection.Reset();
		}
	}

	// Get the next available data store for writing. This data store doesn't have a frame assigned to it yet.
	template<typename AsyncModelDef>
	TAsyncModelDataStore_Input<AsyncModelDef>* GetPendingDataStore_External()
	{
		for (FDataStoreInternal& Item : DataStoreCollections)
		{
			if (Item.Frame == INDEX_NONE)
			{
				return Item.StoreCollection.GetDataStore<TAsyncModelDataStore_Input<AsyncModelDef>, AsyncModelDef>();
			}
		}
		
		return DataStoreCollections.AddDefaulted_GetRef().StoreCollection.GetDataStore<TAsyncModelDataStore_Input<AsyncModelDef>, AsyncModelDef>();
	}

	// Get the next available data store and assign a final frame number to it.	
	void FinalizePendingDataStore_External(int32 Frame)
	{
		FDataStoreInternal* Found = nullptr;
		for (FDataStoreInternal& Item : DataStoreCollections)
		{
			if (Item.Frame == INDEX_NONE)
			{
				Found = &Item;
				break;
			}
		}

		if (!Found)
		{
			Found = &DataStoreCollections.AddDefaulted_GetRef();
		}

		npCheckSlow(Found);
		Found->Frame = Frame;
	}

	// Get data store for reading on the PT, by frame number
	template<typename AsyncModelDef>
	const TAsyncModelDataStore_Input<AsyncModelDef>* GetDataStoreChecked(int32 Frame) const
	{
		return const_cast<FNetworkPredictionSimCallbackInput*>(this)->GetDataStoreCheckedImpl<AsyncModelDef>(Frame);
	}

	template<typename AsyncModelDef>
	TAsyncModelDataStore_Input<AsyncModelDef>* GetDataStoreChecked(int32 Frame)
	{
		return GetDataStoreCheckedImpl<AsyncModelDef>(Frame);
	}

private:

	struct FDataStoreInternal
	{
		int32 Frame = INDEX_NONE;
		FAsyncDataStoreCollection StoreCollection;
	};


	TArray<FDataStoreInternal, TInlineAllocator<InlineSimObjInputs>> DataStoreCollections;


	template<typename AsyncModelDef>
	TAsyncModelDataStore_Input<AsyncModelDef>* GetDataStoreCheckedImpl(int32 Frame)
	{
		for (FDataStoreInternal& Item : DataStoreCollections)
		{
			if (Item.Frame == Frame)
			{
				return Item.StoreCollection.GetDataStore<TAsyncModelDataStore_Input<AsyncModelDef>, AsyncModelDef>();
			}
		}
		
		for (FDataStoreInternal& Item : DataStoreCollections)
		{
			UE_LOG(LogNetworkPrediction, Warning, TEXT("   Found: %d"), Item.Frame);
		}

		npEnsureMsgf(false, TEXT("Could not find DataStore asigned to frame %d. 0x%X"), Frame, (int64)this);
		return nullptr;
	}
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