/*=============================================================================
	PostProcessScreenSpaceReflection.h
=============================================================================*/

#pragma once
#include "CoreMinimal.h"
#include "RendererInterface.h"

struct FScreenSpaceReflectionMobileOutputs
{
	TRefCountPtr<IPooledRenderTarget> ScreenSpaceReflectionTexture;

	bool IsValid()
	{
		return ScreenSpaceReflectionTexture.IsValid();
	}

	void Release()
	{
		ScreenSpaceReflectionTexture.SafeRelease();
	}
};

extern FScreenSpaceReflectionMobileOutputs GScreenSpaceReflectionMobileOutputs;