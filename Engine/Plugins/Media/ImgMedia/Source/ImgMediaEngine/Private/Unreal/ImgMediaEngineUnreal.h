// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_ENGINE

#include "ImgMediaEngine.h"

/**
 * FImgMediaEngine used with Unreal.
 */
class FImgMediaEngineUnreal : public FImgMediaEngine
{
public:
	
	IMGMEDIAENGINE_API virtual void GetCameraInfo(TArray<FImgMediaMipMapCameraInfo>& CameraInfos) override;
};

#endif // WITH_ENGINE

