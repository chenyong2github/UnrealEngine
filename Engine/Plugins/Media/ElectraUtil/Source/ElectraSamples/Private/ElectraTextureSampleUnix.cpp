// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"

#if PLATFORM_UNIX

#include "ElectraTextureSample.h"

/**
* Init code for realloacting an image from the the pool
*/
void FElectraTextureSampleUnix::InitializePoolable()
{
	Duration = FTimespan::Zero();
}

/**
*  Return the object to the pool and inform the renderer about this...
*/
void FElectraTextureSampleUnix::ShutdownPoolable()
{
	TSharedPtr<MEDIArendererVideoUE, ESPMode::ThreadSafe> lockedVideoRenderer = OwningRenderer.Pin();
	if (lockedVideoRenderer.IsValid())
	{
		lockedVideoRenderer->TextureReleasedToPool(GetDuration());
	}

	// This is coming from the original code: Drop reference to the texture. It should be released by the outside system.
	Texture = nullptr;
}

#endif