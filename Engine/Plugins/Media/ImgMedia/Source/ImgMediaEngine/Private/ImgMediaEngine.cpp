// Copyright Epic Games, Inc. All Rights Reserved.

#include "ImgMediaEngine.h"

#include "ImgMediaEngineUnreal.h"
#include "ImgMediaMipMapInfo.h"
#include "MediaTexture.h"

FImgMediaEngine& FImgMediaEngine::Get()
{
#if WITH_ENGINE
	static FImgMediaEngineUnreal Engine;
#else
	static FImgMediaEngine Engine;
#endif

	return Engine;
}

void FImgMediaEngine::RegisterTexture(TSharedPtr<FImgMediaMipMapObjectInfo, ESPMode::ThreadSafe>& InInfo, UMediaTexture* InTexture)
{
	// Do we have this media texture yet?
	TWeakObjectPtr<UMediaTexture> TexturePtr(InTexture);
	if (MediaTextures.Contains(TexturePtr) == false)
	{
		MediaTextures.Emplace(TexturePtr);
		MapTextureToObject.Emplace(TexturePtr);
	}

	// Add component to our list.
	TArray<TWeakPtr<FImgMediaMipMapObjectInfo, ESPMode::ThreadSafe>>* FoundObjects = MapTextureToObject.Find(TexturePtr);
	FoundObjects->Add(InInfo);
}

void FImgMediaEngine::UnregisterTexture(TSharedPtr<FImgMediaMipMapObjectInfo, ESPMode::ThreadSafe>& InInfo, UMediaTexture* InTexture)
{
	TWeakObjectPtr<UMediaTexture> TexturePtr(InTexture);
	TArray<TWeakPtr<FImgMediaMipMapObjectInfo, ESPMode::ThreadSafe>>* FoundObjects = MapTextureToObject.Find(TexturePtr);
	if (FoundObjects != nullptr)
	{
		FoundObjects->RemoveSwap(InInfo);
	}
}

const TArray<TWeakPtr<FImgMediaMipMapObjectInfo, ESPMode::ThreadSafe>>* FImgMediaEngine::GetObjects(UMediaTexture* InTexture) const
{
	const TArray<TWeakPtr<FImgMediaMipMapObjectInfo, ESPMode::ThreadSafe>>* ObjectsPtr = MapTextureToObject.Find(InTexture);
	return ObjectsPtr;
}
