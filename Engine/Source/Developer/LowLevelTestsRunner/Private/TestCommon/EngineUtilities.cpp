// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_ENGINE

#include "TestCommon/EngineUtilities.h"

void InitAsyncQueues()
{
	check(!GDistanceFieldAsyncQueue);
	GDistanceFieldAsyncQueue = new FDistanceFieldAsyncQueue();

	check(!GCardRepresentationAsyncQueue);
	GCardRepresentationAsyncQueue = new FCardRepresentationAsyncQueue();
}

void InitRendering()
{
	FShaderParametersMetadata::InitializeAllUniformBufferStructs();

	{
		// Initialize the RHI.
		const bool bHasEditorToken = false;
		RHIInit(bHasEditorToken);
	}

	{
		// One-time initialization of global variables based on engine configuration.
		RenderUtilsInit();
	}
}
#endif // WITH_ENGINE
