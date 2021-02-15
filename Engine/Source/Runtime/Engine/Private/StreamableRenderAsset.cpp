// Copyright Epic Games, Inc. All Rights Reserved.

#include "Engine/StreamableRenderAsset.h"
#include "Misc/App.h"
#include "ContentStreaming.h"

extern bool TrackRenderAssetEvent(struct FStreamingRenderAsset* StreamingRenderAsset, UStreamableRenderAsset* RenderAsset, bool bForceMipLevelsToBeResident, const FRenderAssetStreamingManager* Manager);

UStreamableRenderAsset::UStreamableRenderAsset(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	check(sizeof(FStreamableRenderResourceState) == sizeof(uint64));
}

void UStreamableRenderAsset::RegisterMipLevelChangeCallback(UPrimitiveComponent* Component, int32 LODIndex, float TimeoutSecs, bool bOnStreamIn, FLODStreamingCallback&& Callback)
{
	check(IsInGameThread());

	if (StreamingIndex != INDEX_NONE)
	{
		const int32 ExpectedResidentMips = CachedSRRState.MaxNumLODs - (LODIndex - CachedSRRState.AssetLODBias);
		if (bOnStreamIn == (CachedSRRState.NumResidentLODs >= ExpectedResidentMips))
		{
			Callback(Component, this, ELODStreamingCallbackResult::Success);
			return;
		}
		
		new (MipChangeCallbacks) FLODStreamingCallbackPayload(Component, FApp::GetCurrentTime() + TimeoutSecs, ExpectedResidentMips, bOnStreamIn, MoveTemp(Callback));
	}
	else
	{
		Callback(Component, this, ELODStreamingCallbackResult::StreamingDisabled);
	}
}

void UStreamableRenderAsset::RemoveMipLevelChangeCallback(UPrimitiveComponent* Component)
{
	check(IsInGameThread());

	for (int32 Idx = 0; Idx < MipChangeCallbacks.Num(); ++Idx)
	{
		if (MipChangeCallbacks[Idx].Component == Component)
		{
			MipChangeCallbacks[Idx].Callback(Component, this, ELODStreamingCallbackResult::ComponentRemoved);
			MipChangeCallbacks.RemoveAtSwap(Idx--);
		}
	}
}

void UStreamableRenderAsset::RemoveAllMipLevelChangeCallbacks()
{
	for (int32 Idx = 0; Idx < MipChangeCallbacks.Num(); ++Idx)
	{
		const FLODStreamingCallbackPayload& Payload = MipChangeCallbacks[Idx];
		Payload.Callback(Payload.Component, this, ELODStreamingCallbackResult::AssetRemoved);
	}
	MipChangeCallbacks.Empty();
}

void UStreamableRenderAsset::TickMipLevelChangeCallbacks(TArray<UStreamableRenderAsset*>* DeferredTickCBAssets)
{
	if (MipChangeCallbacks.Num() > 0)
	{
		if (DeferredTickCBAssets)
		{
			DeferredTickCBAssets->Add(this);
			return;
		}

		const double Now = FApp::GetCurrentTime();
		const int32 ResidentMips = CachedSRRState.NumResidentLODs;

		for (int32 Idx = 0; Idx < MipChangeCallbacks.Num(); ++Idx)
		{
			const FLODStreamingCallbackPayload& Payload = MipChangeCallbacks[Idx];

			if (Payload.bOnStreamIn == (ResidentMips >= Payload.ExpectedResidentMips))
			{
				Payload.Callback(Payload.Component, this, ELODStreamingCallbackResult::Success);
				MipChangeCallbacks.RemoveAt(Idx--);
				continue;
			}

			if (Now > Payload.Deadline)
			{
				Payload.Callback(Payload.Component, this, ELODStreamingCallbackResult::TimedOut);
				MipChangeCallbacks.RemoveAt(Idx--);
			}
		}
	}
}

void UStreamableRenderAsset::TickStreaming(bool bSendCompletionEvents, TArray<UStreamableRenderAsset*>* DeferredTickCBAssets)
{
	// if resident and requested mip counts match then no pending request is in flight
	if (PendingUpdate)
	{
		// When there is no renderthread, allow the gamethread to tick as the renderthread.
		PendingUpdate->Tick(GIsThreadedRendering ? FRenderAssetUpdate::TT_None : FRenderAssetUpdate::TT_Render);

		if (PendingUpdate->IsCompleted())
		{
			if (PendingUpdate->IsSuccessfullyFinished())
			{
				CachedSRRState.NumResidentLODs = CachedSRRState.NumRequestedLODs;
			}
			else
			{
				check(PendingUpdate->IsCancelled());
				CachedSRRState.NumRequestedLODs = CachedSRRState.NumResidentLODs;
			}

			PendingUpdate.SafeRelease();

#if WITH_EDITOR
			if (GIsEditor && bSendCompletionEvents)
			{
				// When all the requested mips are streamed in, generate an empty property changed event, to force the
				// ResourceSize asset registry tag to be recalculated.
				FPropertyChangedEvent EmptyPropertyChangedEvent(nullptr);
				FCoreUObjectDelegates::OnObjectPropertyChanged.Broadcast(this, EmptyPropertyChangedEvent);
			}
#endif
		}
	}

	if (bSendCompletionEvents)
	{
		TickMipLevelChangeCallbacks(DeferredTickCBAssets);
	}
}

void UStreamableRenderAsset::SetForceMipLevelsToBeResident(float Seconds, int32 CinematicLODGroupMask)
{
	const int32 LODGroup = GetLODGroupForStreaming();
	if (CinematicLODGroupMask && LODGroup >= 0 && LODGroup < UE_ARRAY_COUNT(FMath::BitFlag))
	{
		const uint32 TextureGroupBitfield = (uint32)CinematicLODGroupMask;
		bUseCinematicMipLevels = !!(TextureGroupBitfield & FMath::BitFlag[LODGroup]);
	}
	else
	{
		bUseCinematicMipLevels = false;
	}

	ForceMipLevelsToBeResidentTimestamp = FApp::GetCurrentTime() + Seconds;
}

void UStreamableRenderAsset::CancelPendingStreamingRequest()
{
	if (PendingUpdate && !PendingUpdate->IsCancelled())
	{
		PendingUpdate->Abort();
	}
}

bool UStreamableRenderAsset::HasPendingInitOrStreaming(bool bWaitForLODTransition) const 
{
	if (!!PendingUpdate)
	{
		return true;
	}

	if (CachedSRRState.IsValid())
	{
		// Avoid a cache miss unless the hint suggests Init could be pending.
		if (CachedSRRState.bHasPendingInitHint)
		{
			if (HasPendingRenderResourceInitialization())
			{
				return true;
			}
			else if (IsInGameThread())
			{
				CachedSRRState.bHasPendingInitHint = false;
			}
		}
		if (bWaitForLODTransition && CachedSRRState.bHasPendingLODTransitionHint)
		{
			if (HasPendingLODTransition())
			{
				return true;
			}
			else if (IsInGameThread())
			{
				CachedSRRState.bHasPendingLODTransitionHint = false;
			}
		}
		return false;
	}

	return HasPendingRenderResourceInitialization() || (bWaitForLODTransition && HasPendingLODTransition());
}

/** Whether there is a pending update and it is locked within an update step. Used to prevent dealocks in SuspendRenderAssetStreaming(). */
bool UStreamableRenderAsset::IsPendingStreamingRequestLocked() const
{
	return PendingUpdate && PendingUpdate->IsLocked();
}

void UStreamableRenderAsset::LinkStreaming()
{
	// Note that this must be called after InitResource() otherwise IsStreamable will always be false.
	if (!IsTemplate() && RenderResourceSupportsStreaming() && IStreamingManager::Get().IsRenderAssetStreamingEnabled(GetRenderAssetType()))
	{
		if (StreamingIndex == INDEX_NONE)
		{
			IStreamingManager::Get().GetRenderAssetStreamingManager().AddStreamingRenderAsset(this);
		}
	}
	else
	{
		UnlinkStreaming();
	}
}

void UStreamableRenderAsset::UnlinkStreaming()
{
	if (StreamingIndex != INDEX_NONE)
	{
		IStreamingManager::Get().GetRenderAssetStreamingManager().RemoveStreamingRenderAsset(this);
		// Reset the timer effect from SetForceMipLevelsToBeResident()
		ForceMipLevelsToBeResidentTimestamp = 0;
		// No more streaming events can happen now
		RemoveAllMipLevelChangeCallbacks();
	}
}

bool UStreamableRenderAsset::IsFullyStreamedIn()
{
	// Note that if CachedSRRState is not valid, then this asset is not streamable and is then at max resolution.
	if (!CachedSRRState.IsValid() 
		|| !CachedSRRState.bSupportsStreaming 
		|| (CachedSRRState.NumResidentLODs >= (CachedSRRState.MaxNumLODs - CachedCombinedLODBias)))
	{
		return true;
	}

	// IsFullyStreamedIn() might be used incorrectly if any logic waits on it to be true.
	ensureMsgf(CachedSRRState.NumResidentLODs != CachedSRRState.NumNonOptionalLODs, TEXT("IsFullyStreamedIn() is being called on (%s) which might not have optional LODs mounted."), *GetFName().ToString());

	return false;
}

void UStreamableRenderAsset::WaitForPendingInitOrStreaming(bool bWaitForLODTransition, bool bSendCompletionEvents)
{
	while (HasPendingInitOrStreaming(bWaitForLODTransition))
	{
		ensure(!IsAssetStreamingSuspended());

		// Advance the streaming state.
		TickStreaming(bSendCompletionEvents);
		// Make sure any render commands are executed, in particular things like InitRHI, or asset updates on the render thread.
		FlushRenderingCommands();
		// Give some time increment so that LOD transition can complete, and also for the gamethread to give room for streaming async tasks.
		FPlatformProcess::Sleep(RENDER_ASSET_STREAMING_SLEEP_DT);
	}
}

void UStreamableRenderAsset::WaitForStreaming(bool bWaitForLODTransition, bool bSendCompletionEvents)
{
	// Complete pending streaming so that the asset can executing new requests if needed.
	WaitForPendingInitOrStreaming(bWaitForLODTransition, bSendCompletionEvents);

	if (IsStreamable())
	{
		// Update the streamer state for this asset and execute new requests if needed. For example force loading to all LODs.
		IStreamingManager::Get().GetRenderAssetStreamingManager().UpdateIndividualRenderAsset(this);
		// Wait for any action to complete.
		WaitForPendingInitOrStreaming(bWaitForLODTransition, bSendCompletionEvents);
	}
}

void UStreamableRenderAsset::BeginDestroy()
{
	Super::BeginDestroy();

	// Abort any pending streaming operation.
	CancelPendingStreamingRequest();

	// Safely unlink the asset from list of streamable.
	UnlinkStreaming();

	// Remove from the list of tracked assets if necessary
	TrackRenderAssetEvent(nullptr, this, false, nullptr);
}

bool UStreamableRenderAsset::IsReadyForFinishDestroy()
{
	if (!Super::IsReadyForFinishDestroy())
	{
		return false;
	}

	if (PendingUpdate)
	{
		// To avoid async tasks from timing-out the GC, we tick as Async to force completion if this is relevant.
		// This could lead the asset from releasing the PendingUpdate, which will be deleted once the async task completes.
		if (PendingUpdate->GetRelevantThread() == FRenderAssetUpdate::TT_Async)
		{
			PendingUpdate->Tick(FRenderAssetUpdate::TT_GameRunningAsync);
		}
		else
		{
			PendingUpdate->Tick(GIsThreadedRendering ? FRenderAssetUpdate::TT_None : FRenderAssetUpdate::TT_Render);
		}

		if (PendingUpdate->IsCompleted())
		{
			PendingUpdate.SafeRelease();
		}
	}

	return !PendingUpdate.IsValid();
}