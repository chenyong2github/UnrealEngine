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
