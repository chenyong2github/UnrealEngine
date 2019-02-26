// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
RenderAssetUpdate.cpp: Base class of helpers to stream in and out texture/mesh LODs.
=============================================================================*/

#include "Streaming/RenderAssetUpdate.h"
#include "Engine/StreamableRenderAsset.h"
#include "Streaming/TextureStreamingHelpers.h"
#include "UObject/UObjectIterator.h"

float GStreamingFlushTimeOut = 3.00f;
static FAutoConsoleVariableRef CVarStreamingFlushTimeOut(
	TEXT("r.Streaming.FlushTimeOut"),
	GStreamingFlushTimeOut,
	TEXT("Time before we timeout when flushing streaming (default=3)"),
	ECVF_Default
);


volatile int32 GRenderAssetStreamingSuspension = 0;

bool IsAssetStreamingSuspended()
{
	return GRenderAssetStreamingSuspension > 0;
}


void SuspendRenderAssetStreaming()
{
	ensure(IsInGameThread());

	if (FPlatformAtomics::InterlockedIncrement(&GRenderAssetStreamingSuspension) == 1)
	{
		bool bHasPendingUpdate = false;

		// Wait for all assets to have their update lock unlocked. 
		TArray<UStreamableRenderAsset*> LockedAssets;
		for (TObjectIterator<UStreamableRenderAsset> It; It; ++It)
		{
			UStreamableRenderAsset* CurrentAsset = *It;
			if (CurrentAsset && CurrentAsset->HasPendingUpdate())
			{
				bHasPendingUpdate = true;
				if (CurrentAsset->IsPendingUpdateLocked())
				{
					LockedAssets.Add(CurrentAsset);
				}
			}
		}

		// If an asset stays locked for  GStreamingFlushTimeOut, 
		// we conclude there is a deadlock or that the object is never going to recover.

		const float TimeIncrement = 0.010f;
		float TimeLimit = GStreamingFlushTimeOut;

		while (LockedAssets.Num() && (TimeLimit > 0 || GStreamingFlushTimeOut <= 0))
		{
			FPlatformProcess::Sleep(TimeIncrement);
			FlushRenderingCommands();
			
			TimeLimit -= TimeIncrement;

			for (int32 LockedIndex = 0; LockedIndex < LockedAssets.Num(); ++LockedIndex)
			{
				UStreamableRenderAsset* CurrentAsset = LockedAssets[LockedIndex];
				if (!CurrentAsset || !CurrentAsset->IsPendingUpdateLocked())
				{
					LockedAssets.RemoveAtSwap(LockedIndex);
					--LockedIndex;
				}
			}
		}

		if (TimeLimit <= 0 && GStreamingFlushTimeOut > 0)
		{
			UE_LOG(LogContentStreaming, Error, TEXT("SuspendRenderAssetStreaming timed out while waiting for asset:"));
			for (int32 LockedIndex = 0; LockedIndex < LockedAssets.Num(); ++LockedIndex)
			{
				UStreamableRenderAsset* CurrentAsset = LockedAssets[LockedIndex];
				if (CurrentAsset)
				{
					if (CurrentAsset->IsPendingKill() || CurrentAsset->HasAnyFlags(RF_BeginDestroyed|RF_FinishDestroyed))
					{
						UE_LOG(LogContentStreaming, Error, TEXT("	%s"), *CurrentAsset->GetFullName());
					}
					else
					{
						UE_LOG(LogContentStreaming, Error, TEXT("	%s (PendingKill)"), *CurrentAsset->GetFullName());
					}
				}
			}
		}

		// At this point, no more rendercommands or IO requests can be generated before a call to ResumeRenderAssetStreamingRenderTasksInternal().

		if (bHasPendingUpdate)
		{
			// Ensure any pending render command executes.
			FlushRenderingCommands();
		}
	}
}

void ResumeRenderAssetStreaming()
{
	FPlatformAtomics::InterlockedDecrement(&GRenderAssetStreamingSuspension);
	ensure(GRenderAssetStreamingSuspension >= 0);
}
