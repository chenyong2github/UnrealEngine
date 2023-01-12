// Copyright Epic Games, Inc. All Rights Reserved.

#include "RHIResources.h"
#include "RHI.h"

std::atomic<TClosableMpscQueue<FRHIResource*>*> FRHIResource::PendingDeletes{ new TClosableMpscQueue<FRHIResource*>() };
FHazardPointerCollection FRHIResource::PendingDeletesHPC;
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
	TClosableMpscQueue<FRHIResource*>* PendingDeletesPtr = PendingDeletes.exchange(new TClosableMpscQueue<FRHIResource*>());
	PendingDeletesPtr->Close([&DeletedResources](FRHIResource* Resource)
	{
		DeletedResources.Push(Resource);
	});
	PendingDeletesHPC.Delete(PendingDeletesPtr);

	const int32 NumDeletes = DeletedResources.Num();

	RHICmdList.EnqueueLambda([DeletedResources = MoveTemp(DeletedResources)](FRHICommandListImmediate& RHICmdList) mutable
	{
		if (GDynamicRHI)
		{
			GDynamicRHI->RHIPerFrameRHIFlushComplete();
		}

		for (FRHIResource* Resource : DeletedResources)
		{
			if (Resource->AtomicFlags.Deleteing())
			{
				FRHIResource::CurrentlyDeleting = Resource;
				delete Resource;
			}
		}
	});

	return NumDeletes;
}
