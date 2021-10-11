// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "NetworkPredictionAsyncDataStore.h"
#include "NetworkPredictionAsyncModelDef.h"
#include "NetworkPredictionUtil.h"
#include "Containers/CircularBuffer.h"
#include "NetworkPredictionAsyncID.h"
#include "NetworkPredictionAsyncPersistentStorage.h"
#include "NetworkPredictionAsyncDefines.h"
#include "Misc/EnumClassFlags.h"

namespace UE_NP {

enum class ENetworkPredictionAsyncInputSource : uint8 // fixme: move
{
	None,		// There no gt-created input for this instance
	Local,		// Input comes from the PendingInputCmd* that was given to us
	Buffered	// Input comes from the PendingInputCmdBuffer in our internal data store
};

enum class EInstanceFlags
{
	None				= 0,
	LocallyControlled	= 1 << 0,	// Instance is locally controlled
};
ENUM_CLASS_FLAGS(EInstanceFlags);

// Static data about an instance (data that we don't track changes frame-to-frame)
template<typename AsyncModelDef>
struct TAsncInstanceStaticData
{
	HOIST_ASYNCMODELDEF_TYPES()

	uint16 Index;
	LocalStateType LocalState;
	int32 LocalSpawnFrame = INDEX_NONE;
};

template<typename AsyncModelDef>
struct TAsyncFrameInstanceData
{
	HOIST_ASYNCMODELDEF_TYPES()

	InputCmdType InputCmd;
	NetStateType NetState;
};

// A snapshot of the world for a given frame (only contains per-frame instance data)
template<typename AsyncModelDef>
struct TAsncFrameSnapshot
{
	HOIST_ASYNCMODELDEF_TYPES();

	TArray<InputCmdType> InputCmds;
	TArray<NetStateType> NetStates;

	void Reset()
	{
		InputCmds.Reset();
		NetStates.Reset();
	}
};

template<typename AsyncModelDef>
struct TAsyncPendingInputCmdPtr
{
	HOIST_ASYNCMODELDEF_TYPES()

	int32 Index;
	InputCmdType* PendingInputCmd = nullptr;
	
	bool operator==(const int32& idx) const
	{
		return Index == idx;
	}
};

template<typename AsyncModelDef>
struct TAsyncPendingInputCmdBuffer
{
	HOIST_ASYNCMODELDEF_TYPES()

	int32 Index;
	TStaticArray<InputCmdType, InputCmdBufferSize> Buffer;

	bool operator==(const int32& idx) const
	{
		return Index == idx;
	}
};

template<typename AsyncModelDef>
struct TAsyncOutputWriteTargets
{
	HOIST_ASYNCMODELDEF_TYPES()

	NetStateType* OutNetState = nullptr;
};

template<typename AsyncModelDef>
struct TAsyncNetRecvData
{
	HOIST_ASYNCMODELDEF_TYPES()

	struct FInstance
	{
		mutable int32 Frame = 0;
		uint8 Flags = 0;
		InputCmdType InputCmd;
		NetStateType NetState;

		InputCmdType LatestInputCmd; // latest, unprocessed InputCmd from server
	};

	TSparseArray<FInstance>	NetRecvInstances;

	void Reset()
	{
		NetRecvInstances.Reset();
	}
};

template<typename AsyncModelDef>
struct TAsyncCorrectionData
{
	HOIST_ASYNCMODELDEF_TYPES()

	struct FInstance
	{
		int32 Index = INDEX_NONE;
		InputCmdType InputCmd;
		NetStateType NetState;
	};

	int32 Frame;
	TArray<FInstance> Instances;
};

template<typename AsyncModelDef>
struct TAsyncLocalStateMod
{
	HOIST_ASYNCMODELDEF_TYPES()

	TAsyncLocalStateMod(FNetworkPredictionAsyncID id, TUniqueFunction<void(LocalStateType&)> f)
		: ID(id), Func(MoveTemp(f)) {}

	FNetworkPredictionAsyncID ID;
	TUniqueFunction<void(LocalStateType&)> Func;

	bool operator==(const FNetworkPredictionAsyncID& id) const { return ID == id; }
};

template<typename AsyncModelDef>
struct TAsyncNetStateMod
{
	HOIST_ASYNCMODELDEF_TYPES()

	TAsyncNetStateMod(int32 i, TUniqueFunction<void(NetStateType&)> f)
		: Index(i), Func(MoveTemp(f)) {}

	int32 Index;
	TUniqueFunction<void(NetStateType&)> Func;
};

// ---------------------------------------------------------------------
// DataStore for Marshaled data GT->PT (lives on the SimCallback Input)
// ---------------------------------------------------------------------

template<typename AsyncModelDef>
struct TAsyncModelDataStore_Input
{
	HOIST_ASYNCMODELDEF_TYPES()

	struct FNewInstance
	{
		FNetworkPredictionAsyncID ID;
		TAsncInstanceStaticData<AsyncModelDef> StaticData;
		NetStateType NetState;
	};

	struct FLocalInputCmd
	{
		FLocalInputCmd(int32 InIndex, InputCmdType& InInputCmd)
			: Index(InIndex), InputCmd(InInputCmd) {}
		int32 Index;
		InputCmdType InputCmd;
	};

	TArray<FNewInstance> NewInstances;
	TArray<TAsyncLocalStateMod<AsyncModelDef>> LocalStateMods;
	TArray<TAsyncNetStateMod<AsyncModelDef>> NetStateMods;
	TArray<FLocalInputCmd> LocalInputCmds;
	TArray<FNetworkPredictionAsyncID> DeletedInstances;

	void Reset()
	{
		NewInstances.Reset();
		LocalStateMods.Reset();
		NetStateMods.Reset();
		LocalInputCmds.Reset();
		DeletedInstances.Reset();
	}
};

// ---------------------------------------------------------------------
// DataStore for Marshaled data PT->GT (lives on the SimCallback Output)
// ---------------------------------------------------------------------

template<typename AsyncModelDef>
struct TAsyncModelDataStore_Output
{
	void Reset()
	{
		Snapshot.Reset();
	}

	TAsncFrameSnapshot<AsyncModelDef> Snapshot;
};

// ---------------------------------------------------------------------
// DataStore for internal data (Accessible on the PT)
// ---------------------------------------------------------------------

// The internal data store that is accessible on the PT
template<typename AsyncModelDef>
struct TAsyncModelDataStore_Internal
{
	HOIST_ASYNCMODELDEF_TYPES()	
	TSortedMap<FNetworkPredictionAsyncID, TAsncInstanceStaticData<AsyncModelDef>> Instances;
	TFrameStorage<TAsncFrameSnapshot<AsyncModelDef>> Frames;

	TQueue<TAsyncNetRecvData<AsyncModelDef>> NetRecvQueue;

	TStaticArray<TAsyncCorrectionData<AsyncModelDef>, UE_NP::NumFramesStorage> Corrections;

	void Reset()
	{
		Instances.Reset();
	}
};

// External: for the game thread
template<typename AsyncModelDef>
struct TAsyncModelDataStore_External
{
	HOIST_ASYNCMODELDEF_TYPES()

	TSortedMap<FNetworkPredictionAsyncID, TAsncInstanceStaticData<AsyncModelDef>> Instances;
	TBitArray<> FreeIndices;

	TAsncFrameSnapshot<AsyncModelDef> LatestSnapshot;

	TArray<TAsyncPendingInputCmdPtr<AsyncModelDef>>	InActivePendingInputCmds;	
	TArray<TAsyncPendingInputCmdPtr<AsyncModelDef>>	ActivePendingInputCmds; // PendingINputCmds that get marshaled directly to PT
	TSortedMap<FNetworkPredictionAsyncID, TUniqueObj<TAsyncPendingInputCmdBuffer<AsyncModelDef>>> PendingInputCmdBuffers; // GT buffered InputCmds

	TArray<TAsyncOutputWriteTargets<AsyncModelDef>>	OutputWriteTargets;

	TAsyncNetRecvData<AsyncModelDef> PendingNetRecv;	// Network Data is written here (clients, receiving)

	TArray<TAsyncLocalStateMod<AsyncModelDef>> DeferredLocalStateMods;
	
	void Reset()
	{
		Instances.Reset();
		FreeIndices.Reset();
		LatestSnapshot.Reset();
		InActivePendingInputCmds.Reset();
		ActivePendingInputCmds.Reset();
		PendingInputCmdBuffers.Reset();
		OutputWriteTargets.Reset();
		PendingNetRecv.Reset();
		DeferredLocalStateMods.Reset();
	}
};




} // namespace UE_NP

