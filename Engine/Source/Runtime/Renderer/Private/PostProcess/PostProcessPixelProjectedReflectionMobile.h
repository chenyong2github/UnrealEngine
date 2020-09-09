// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	PostProcessPixelProjectedReflectionMobile.h
=============================================================================*/

#pragma once
#include "CoreMinimal.h"
#include "RendererInterface.h"

#define PROJECTION_OUTPUT_TYPE_BUFFER 0

#define PROJECTION_OUTPUT_TYPE_TEXTURE 1

#if PLATFORM_IOS || PLATFORM_MAC
#define PROJECTION_OUTPUT_TYPE PROJECTION_OUTPUT_TYPE_BUFFER
#else
#define PROJECTION_OUTPUT_TYPE PROJECTION_OUTPUT_TYPE_TEXTURE
#endif

struct FPixelProjectedReflectionMobileOutputs
{
	TRefCountPtr<IPooledRenderTarget> PixelProjectedReflectionTexture;

	bool IsValid()
	{
		return PixelProjectedReflectionTexture.IsValid();
	}

	void Release()
	{
		PixelProjectedReflectionTexture.SafeRelease();
	}
};

extern FPixelProjectedReflectionMobileOutputs GPixelProjectedReflectionMobileOutputs;