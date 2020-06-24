// Copyright Epic Games, Inc. All Rights Reserved.

#include "Engine/StreamableRenderAsset.h"
#include "Misc/App.h"


UStreamableRenderAsset::UStreamableRenderAsset(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, StreamingIndex(INDEX_NONE)
{}

void UStreamableRenderAsset::RegisterMipLevelChangeCallback(UPrimitiveComponent* Component, int32 LODIdx, float TimeoutSecs, bool bOnStreamIn, FLODStreamingCallback&& Callback)
{
	check(IsInGameThread());

	if (StreamingIndex != INDEX_NONE)
	{
		const int32 MipCount = GetNumMipsForStreaming();
		const int32 ResidentMips = GetCachedNumResidentLODs();
		const int32 ExpectedResidentMips = MipCount - LODIdx;

		if ((bOnStreamIn && ResidentMips >= ExpectedResidentMips) || (!bOnStreamIn && ResidentMips < ExpectedResidentMips))
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
		const int32 ResidentMips = GetCachedNumResidentLODs();

		for (int32 Idx = 0; Idx < MipChangeCallbacks.Num(); ++Idx)
		{
			const FLODStreamingCallbackPayload& Payload = MipChangeCallbacks[Idx];

			if ((Payload.bOnStreamIn && ResidentMips >= Payload.ExpectedResidentMips) || (!Payload.bOnStreamIn && ResidentMips < Payload.ExpectedResidentMips))
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
