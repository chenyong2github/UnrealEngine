// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Engine/StreamableRenderAsset.h"
#include "Misc/App.h"


UStreamableRenderAsset::UStreamableRenderAsset(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, StreamingIndex(INDEX_NONE)
{}

void UStreamableRenderAsset::SetForceMipLevelsToBeResident(float Seconds, int32 CinematicLODGroupMask)
{
	uint32 TextureGroupBitfield = (uint32)CinematicLODGroupMask;
	int32 LODGroup = GetLODGroupForStreaming();
	uint32 MyTextureGroup = FMath::BitFlag[LODGroup];
	bUseCinematicMipLevels = (TextureGroupBitfield & MyTextureGroup) ? true : false;
	ForceMipLevelsToBeResidentTimestamp = FApp::GetCurrentTime() + Seconds;
}
