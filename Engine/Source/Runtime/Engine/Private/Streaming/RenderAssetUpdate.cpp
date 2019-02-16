// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
RenderAssetUpdate.cpp: Base class of helpers to stream in and out texture/mesh LODs.
=============================================================================*/

#include "Streaming/RenderAssetUpdate.h"

volatile int32 GRenderAssetStreamingSuspendRenderThreadTasks;

void SuspendRenderAssetStreamingRenderTasksInternal()
{
	// This doesn't prevent from having a task pushed immediately after as some threads could already be deep in FTexture2DUpdate::PushTask.
	// This is why GSuspendRenderThreadTasks also gets checked in FTexture2DUpdate::Tick. The goal being to avoid accessing the RHI more
	// than avoiding pushing new render commands. The reason behind is that some code paths access the RHI outside the render thread.
	FPlatformAtomics::InterlockedIncrement(&GRenderAssetStreamingSuspendRenderThreadTasks);
}

void ResumeRenderAssetStreamingRenderTasksInternal()
{
	FPlatformAtomics::InterlockedDecrement(&GRenderAssetStreamingSuspendRenderThreadTasks);
}
