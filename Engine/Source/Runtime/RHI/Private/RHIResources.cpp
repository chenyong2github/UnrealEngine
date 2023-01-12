// Copyright Epic Games, Inc. All Rights Reserved.

#include "RHIResources.h"
#include "RHI.h"

UE::TConsumeAllMpmcQueue<FRHIResource*> FRHIResource::PendingDeletes;
FRHIResource* FRHIResource::CurrentlyDeleting = nullptr;

bool FRHIResource::Bypass()
{
	return GRHICommandList.Bypass();
}

DECLARE_CYCLE_STAT(TEXT("Delete Resources"), STAT_DeleteResources, STATGROUP_RHICMDLIST);

int32 FRHIResource::FlushPendingDeletes(FRHICommandListImmediate& RHICmdList)
{
	SCOPE_CYCLE_COUNTER(STAT_DeleteResources);

	check(IsInRenderingThread());

	TArray<FRHIResource*> DeletedResources;
	PendingDeletes.ConsumeAllLifo([&DeletedResources](FRHIResource* Resource)
	{
		DeletedResources.Push(Resource);
	});

	const int32 NumDeletes = DeletedResources.Num();

	RHICmdList.EnqueueLambda([DeletedResources = MoveTemp(DeletedResources)](FRHICommandListImmediate& RHICmdList) mutable
	{
		if (GDynamicRHI)
		{
			GDynamicRHI->RHIPerFrameRHIFlushComplete();
		}

		for (int32 i = DeletedResources.Num() - 1; i >= 0; i--)
		{
			FRHIResource* Resource = DeletedResources[i];
			if (Resource->AtomicFlags.Deleteing())
			{
				FRHIResource::CurrentlyDeleting = Resource;
				delete Resource;
			}
		}
	});

	return NumDeletes;
}
