// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	MetalIndexBuffer.cpp: Metal Index buffer RHI implementation.
=============================================================================*/

#include "MetalRHIPrivate.h"
#include "MetalProfiler.h"
#include "MetalCommandBuffer.h"
#include "MetalCommandQueue.h"
#include "Containers/ResourceArray.h"
#include "RenderUtils.h"
#include "HAL/LowLevelMemTracker.h"

void FMetalDynamicRHI::RHITransferBufferUnderlyingResource(FRHIBuffer* DestBuffer, FRHIBuffer* SrcBuffer)
{
	@autoreleasepool {
	check(DestBuffer);
	FMetalResourceMultiBuffer* Dest = ResourceCast(DestBuffer);
	if (!SrcBuffer)
	{
		TRefCountPtr<FMetalResourceMultiBuffer> DeletionProxy = new FMetalResourceMultiBuffer(0, Dest->GetUsage(), Dest->GetStride(), nullptr, Dest->Type);
		Dest->Swap(*DeletionProxy);
	}
	else
	{
		FMetalResourceMultiBuffer* Src = ResourceCast(SrcBuffer);
		Dest->Swap(*Src);
	}
	}
}
