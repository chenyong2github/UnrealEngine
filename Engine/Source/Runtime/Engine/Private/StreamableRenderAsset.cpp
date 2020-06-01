// Copyright Epic Games, Inc. All Rights Reserved.

#include "Engine/StreamableRenderAsset.h"
#include "Misc/App.h"


UStreamableRenderAsset::UStreamableRenderAsset(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, StreamingIndex(INDEX_NONE)
{}

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
