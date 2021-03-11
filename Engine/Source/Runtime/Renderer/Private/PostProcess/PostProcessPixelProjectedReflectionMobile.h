// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	PostProcessPixelProjectedReflectionMobile.h
=============================================================================*/

#pragma once
#include "CoreMinimal.h"
#include "RendererInterface.h"

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