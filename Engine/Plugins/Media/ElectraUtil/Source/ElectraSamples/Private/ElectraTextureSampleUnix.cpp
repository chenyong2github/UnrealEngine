// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"

#if PLATFORM_UNIX

#include "ElectraTextureSample.h"

#if !UE_SERVER

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
	Texture = nullptr;
}

#endif

#endif