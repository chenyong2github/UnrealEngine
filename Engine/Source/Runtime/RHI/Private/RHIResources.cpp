// Copyright Epic Games, Inc. All Rights Reserved.

#include "RHIResources.h"
#include "Containers/ClosableMpscQueue.h"
#include "RHI.h"
#include "Experimental/Containers/HazardPointer.h"
#include "Misc/MemStack.h"
#include "Stats/Stats.h"

UE::TConsumeAllMpmcQueue<FRHIResource*> FRHIResource::PendingDeletes;
UE::TConsumeAllMpmcQueue<FRHIResource*> FRHIResource::PendingDeletesWithLifetimeExtension;
FRHIResource* FRHIResource::CurrentlyDeleting = nullptr;

bool FRHIResource::Bypass()
{
	return GRHICommandList.Bypass();
}

int32 FRHIResource::FlushPendingDeletes(FRHICommandListImmediate& RHICmdList)
{
	return RHICmdList.FlushPendingDeletes();
}
