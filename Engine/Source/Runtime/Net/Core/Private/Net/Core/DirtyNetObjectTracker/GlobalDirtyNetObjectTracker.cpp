// Copyright Epic Games, Inc. All Rights Reserved.

#include "Net/Core/DirtyNetObjectTracker/GlobalDirtyNetObjectTracker.h"
#include "Containers/Set.h"
#include "Misc/CoreDelegates.h"

namespace UE::Net
{

class FGlobalDirtyNetObjectTracker::FPimpl
{
private:
	FPimpl();
	~FPimpl();

	void ResetDirtyNetObjects();

private:
	friend FGlobalDirtyNetObjectTracker;

	static TSet<FNetHandle> EmptyDirtyObjects;

#if WITH_PUSH_MODEL
	TSet<FNetHandle> DirtyObjects;
	FNetBitArray AssignedHandleIndices;
	FNetBitArray Pollers;
	uint32 PollerCount = 0;
#endif
};

TSet<FNetHandle> FGlobalDirtyNetObjectTracker::FPimpl::EmptyDirtyObjects;

#if WITH_PUSH_MODEL
FGlobalDirtyNetObjectTracker::FPimpl* FGlobalDirtyNetObjectTracker::Instance = nullptr;
#endif

void FGlobalDirtyNetObjectTracker::MarkNetObjectStateDirty(FNetHandle NetHandle)
{
#if WITH_PUSH_MODEL
	if (Instance && Instance->PollerCount > 0)
	{
		Instance->DirtyObjects.Add(NetHandle);
	}
#endif
}

FGlobalDirtyNetObjectTracker::FPollHandle FGlobalDirtyNetObjectTracker::CreatePoller()
{
#if WITH_PUSH_MODEL
	if (Instance)
	{
		if (Instance->PollerCount >= Instance->AssignedHandleIndices.GetNumBits())
		{
			Instance->AssignedHandleIndices.SetNumBits(Instance->PollerCount + 1U);
			Instance->Pollers.SetNumBits(Instance->PollerCount + 1U);
		}

		const uint32 HandleIndex = Instance->AssignedHandleIndices.FindFirstZero();
		if (!ensure(HandleIndex != FNetBitArrayBase::InvalidIndex))
		{
			return FPollHandle();
		}

		Instance->AssignedHandleIndices.SetBit(HandleIndex);
		++Instance->PollerCount;
		return FPollHandle(HandleIndex);
	}
#endif

	return FPollHandle();
}

void FGlobalDirtyNetObjectTracker::DestroyPoller(uint32 HandleIndex)
{
#if WITH_PUSH_MODEL
	if (HandleIndex == FPollHandle::InvalidIndex)
	{
		return;
	}

	if (ensureAlwaysMsgf((HandleIndex < Instance->AssignedHandleIndices.GetNumBits()) && Instance->AssignedHandleIndices.GetBit(HandleIndex), TEXT("Destroying unknown poller with handle index %u"), HandleIndex))
	{
		Instance->AssignedHandleIndices.ClearBit(HandleIndex);

		const uint32 PollerCalled = Instance->Pollers.GetBit(HandleIndex);
		ensureAlwaysMsgf(PollerCalled == 0U, TEXT("Destroying poller that called GetDirtyNetObjects() but not ResetDirtyNetObjects()"));
		Instance->Pollers.ClearBit(HandleIndex);

		--Instance->PollerCount;
		if (Instance->PollerCount <= 0)
		{
			Instance->DirtyObjects.Reset();
		}
	}
#endif
}

const TSet<FNetHandle>& FGlobalDirtyNetObjectTracker::GetDirtyNetObjects(const FPollHandle& Handle)
{
#if WITH_PUSH_MODEL
	if (Instance && Handle.IsValid())
	{
		Instance->Pollers.SetBit(Handle.Index);
		return Instance->DirtyObjects;
	}
#endif

	return FPimpl::EmptyDirtyObjects;
}

void FGlobalDirtyNetObjectTracker::ResetDirtyNetObjects(const FPollHandle& Handle)
{
#if WITH_PUSH_MODEL
	if (Instance && Handle.IsValid())
	{
		Instance->Pollers.ClearBit(Handle.Index);
		if (Instance->Pollers.IsNoBitSet())
		{
			Instance->DirtyObjects.Reset();
		}
	}
#endif
}

void FGlobalDirtyNetObjectTracker::Init()
{
#if WITH_PUSH_MODEL
	checkf(Instance == nullptr, TEXT("%s"), TEXT("Only one FNetHandleManager instance may exist."));
	Instance = new FGlobalDirtyNetObjectTracker::FPimpl();
#endif
}

void FGlobalDirtyNetObjectTracker::Deinit()
{
#if WITH_PUSH_MODEL
	delete Instance;
	Instance = nullptr;
#endif
}

FGlobalDirtyNetObjectTracker::FPimpl::FPimpl()
{
#if WITH_ENGINE
	FCoreDelegates::OnEndFrame.AddRaw(this, &FPimpl::ResetDirtyNetObjects);
#endif
}

FGlobalDirtyNetObjectTracker::FPimpl::~FPimpl()
{
#if WITH_ENGINE
	FCoreDelegates::OnEndFrame.RemoveAll(this);
#endif
}

void FGlobalDirtyNetObjectTracker::FPimpl::ResetDirtyNetObjects()
{
#if WITH_PUSH_MODEL
	if (!ensureMsgf(Pollers.IsNoBitSet(), TEXT("FGlobalDirtyNetObjectTracker poller %u forgot to call ResetDirtNetObjects."), Pollers.FindFirstOne()))
	{
		Pollers.Reset();

		// DirtyObjects should already be reset if there are no pollers.
		DirtyObjects.Reset();
	}
#endif
}

}
