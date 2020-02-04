// Copyright Epic Games, Inc. All Rights Reserved.

#include "StageSettings/PICPStageSettings.h"
#include "Engine/TextureRenderTarget2D.h"


TMap<FString, UTextureRenderTarget2D*> APICPStageSettings::RenderTargetCache;


APICPStageSettings::APICPStageSettings()
{
	PrimaryActorTick.bCanEverTick = true;
}

UTextureRenderTarget2D* APICPStageSettings::GetRenderTargetCached(const FString& RenderTextureId) const
{
	FScopeLock lock(&InternalsSyncScope);

	if (RenderTargetCache.Contains(RenderTextureId))
	{
		return RenderTargetCache[RenderTextureId];
	}

	return nullptr;
}

void APICPStageSettings::SetRenderTargetCached(const FString& RenderTextureId, UTextureRenderTarget2D* RenderTexture)
{
	FScopeLock lock(&InternalsSyncScope);
	RenderTargetCache.Emplace(RenderTextureId, RenderTexture);
}

//! check sharedptr cleanup
void APICPStageSettings::ClearRenderTargetCache()
{
	FScopeLock lock(&InternalsSyncScope);
	RenderTargetCache.Empty();
}
