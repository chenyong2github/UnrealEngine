// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Net/Core/NetBitArray.h"
#include "Net/Core/DirtyNetObjectTracker/GlobalDirtyNetObjectTracker.h"

#include "Iris/IrisConfig.h"

namespace UE::Net::Private
{
	class FNetRefHandleManager;
	class FDirtyObjectsAccessor;
}

namespace UE::Net::Private
{

IRISCORE_API void MarkNetObjectStateDirty(uint32 ReplicationSystemId, uint32 NetObjectIndex);

struct FDirtyNetObjectTrackerInitParams
{
	const FNetRefHandleManager* NetRefHandleManager = nullptr;
	uint32 ReplicationSystemId = 0;
	uint32 MaxObjectCount = 0;
	uint32 NetObjectIndexRangeStart = 0;
	uint32 NetObjectIndexRangeEnd = 0;
};

class FDirtyNetObjectTracker
{
public:
	FDirtyNetObjectTracker();
	~FDirtyNetObjectTracker();

	void Init(const FDirtyNetObjectTrackerInitParams& Params);

	/* Update dirty objects with the set of globally marked dirty objects. **/
	void UpdateDirtyNetObjects();

	/** Set safety permissions so no one can write in the bit array via the public methods */
	void LockExternalAccess();

	/** Release safety permissions and allow to write in the bit array via the public methods */
	void AllowExternalAccess();

	/** Reset the global and local dirty objects lists */
	void ClearDirtyNetObjects();

private:
	friend IRISCORE_API void MarkNetObjectStateDirty(uint32 ReplicationSystemId, uint32 NetObjectIndex);

	friend FDirtyObjectsAccessor;

	using StorageType = FNetBitArrayView::StorageWordType;
	static constexpr uint32 StorageTypeBitCount = FNetBitArrayView::WordBitCount;

	void Deinit();
	void MarkNetObjectDirty(uint32 NetObjectIndex);

	/** Can only be accessed via FDirtyObjectsAccessor */
	FNetBitArrayView GetDirtyNetObjects() const;

	const FNetRefHandleManager* NetRefHandleManager;
	StorageType* DirtyNetObjectContainer;
	FGlobalDirtyNetObjectTracker::FPollHandle GlobalDirtyTrackerPollHandle;
	uint32 ReplicationSystemId;
	uint32 DirtyNetObjectWordCount;
	uint32 NetObjectIdRangeStart;
	uint32 NetObjectIdRangeEnd;
	uint32 NetObjectIdCount;
	
	bool bHasPolledGlobalDirtyTracker = false;

#if UE_NET_THREAD_SAFETY_CHECK
	std::atomic_bool bIsExternalAccessAllowed = false;
#endif
};

class FDirtyObjectsAccessor
{
public:
	FDirtyObjectsAccessor(FDirtyNetObjectTracker& InDirtyNetObjectTracker)
		: DirtyNetObjectTracker(InDirtyNetObjectTracker)
	{
		DirtyNetObjectTracker.LockExternalAccess();
	}

	~FDirtyObjectsAccessor()
	{
		DirtyNetObjectTracker.AllowExternalAccess();
	}

	FNetBitArrayView GetDirtyNetObjects()				{ return DirtyNetObjectTracker.GetDirtyNetObjects(); }
	const FNetBitArrayView GetDirtyNetObjects() const	{ return DirtyNetObjectTracker.GetDirtyNetObjects(); }

private:
	FDirtyNetObjectTracker& DirtyNetObjectTracker;
};

}
