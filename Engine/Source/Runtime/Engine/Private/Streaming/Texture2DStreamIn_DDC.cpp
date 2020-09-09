// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
Texture2DStreamIn_DDC.cpp: Stream in helper for 2D textures loading DDC files.
=============================================================================*/

#include "Streaming/Texture2DStreamIn_DDC.h"
#include "Streaming/TextureStreamingHelpers.h"
#include "RenderUtils.h"
#include "DerivedDataCacheInterface.h"
#include "Serialization/MemoryReader.h"

#if WITH_EDITORONLY_DATA

int32 GStreamingUseAsyncRequestsForDDC = 1;
static FAutoConsoleVariableRef CVarStreamingDDCPendingSleep(
	TEXT("r.Streaming.UseAsyncRequestsForDDC"),
	GStreamingUseAsyncRequestsForDDC,
	TEXT("Whether to use async DDC requets in order to react quickly to cancel and suspend rendering requests (default=0)"),
	ECVF_Default
);

int32 GStreamingAbandonedDDCHandlePurgeFrequency = 150;
static FAutoConsoleVariableRef CVarStreamingAbandonedDDCHandlePurgeFrequency(
	TEXT("r.Streaming.AbandonedDDCHandlePurgeFrequency"),
	GStreamingAbandonedDDCHandlePurgeFrequency,
	TEXT("The number of abandonned handle at which a purge will be triggered (default=150)"),
	ECVF_Default
);

// ******************************************
// ******* FAbandonedDDCHandleManager *******
// ******************************************

FAbandonedDDCHandleManager GAbandonedDDCHandleManager;

void FAbandonedDDCHandleManager::Add(uint32 InHandle)
{
	check(InHandle);

	bool bPurge = false;
	{
		FScopeLock ScopeLock(&CS);
		Handles.Add(InHandle);

		++TotalAdd;
		bPurge = (TotalAdd % GStreamingAbandonedDDCHandlePurgeFrequency == 0);
	}

	if (bPurge)
	{
		Purge();
	}
}

void FAbandonedDDCHandleManager::Purge()
{
	TArray<uint32> TempHandles;	
	{
		FScopeLock ScopeLock(&CS);
		FMemory::Memswap(&TempHandles, &Handles, sizeof(TempHandles));
	}

	FDerivedDataCacheInterface& DDCInterface = GetDerivedDataCacheRef();
	TArray<uint8> Data;

	for (int32 Index = 0; Index < TempHandles.Num(); ++Index)
	{
		uint32 Handle = TempHandles[Index];
		if (DDCInterface.PollAsynchronousCompletion(Handle))
		{
			DDCInterface.GetAsynchronousResults(Handle, Data);
			Data.Reset();

			TempHandles.RemoveAtSwap(Index);
			--Index;
		}
	}

	if (TempHandles.Num())
	{
		FScopeLock ScopeLock(&CS);
		Handles.Append(TempHandles);
	}
}

void PurgeAbandonedDDCHandles()
{
	GAbandonedDDCHandleManager.Purge();
}

// ******************************************
// ********* FTexture2DStreamIn_DDC *********
// ******************************************

FTexture2DStreamIn_DDC::FTexture2DStreamIn_DDC(UTexture2D* InTexture)
	: FTexture2DStreamIn(InTexture)
{
	DDCHandles.AddZeroed(ResourceState.MaxNumLODs);
}

FTexture2DStreamIn_DDC::~FTexture2DStreamIn_DDC()
{
	// On cancellation, we don't wait for DDC requests to complete before releasing the object.
	// This prevents GC from being stalled when texture are deleted.
	for (uint32& AbandonedHandle : DDCHandles)
	{
		if (AbandonedHandle)
		{
			GAbandonedDDCHandleManager.Add(AbandonedHandle);
			AbandonedHandle = 0;
		}
	}
}

void FTexture2DStreamIn_DDC::DoCreateAsyncDDCRequests(const FContext& Context)
{
	if (Context.Texture && Context.Resource)
	{
		for (int32 MipIndex = PendingFirstLODIdx; MipIndex < CurrentFirstLODIdx && !IsCancelled(); ++MipIndex)
		{
			const FTexture2DMipMap& MipMap = *Context.MipsView[MipIndex];
			if (!MipMap.DerivedDataKey.IsEmpty())
			{
				check(!DDCHandles[MipIndex]);
				DDCHandles[MipIndex] = GetDerivedDataCacheRef().GetAsynchronous(*MipMap.DerivedDataKey, Context.Texture->GetPathName());

#if !UE_BUILD_SHIPPING
				// On some platforms the IO is too fast to test cancelation requests timing issues.
				if (FRenderAssetStreamingSettings::ExtraIOLatency > 0 && TaskSynchronization.GetValue() == 0)
				{
					FPlatformProcess::Sleep(FRenderAssetStreamingSettings::ExtraIOLatency * .001f); // Slow down the streaming.
				}
#endif
			}
			else
			{
				UE_LOG(LogTexture, Error, TEXT("DDC key missing."));
				MarkAsCancelled();
			}
		}
	}
}

bool FTexture2DStreamIn_DDC::DoPoolDDCRequests(const FContext& Context) 
{
	for (int32 MipIndex = PendingFirstLODIdx; MipIndex < CurrentFirstLODIdx && !IsCancelled(); ++MipIndex)
	{
		const uint32 Handle = DDCHandles[MipIndex];
		if (Handle && !GetDerivedDataCacheRef().PollAsynchronousCompletion(Handle))
		{
			return false;
		}
	}
	return true;
}

void FTexture2DStreamIn_DDC::DoLoadNewMipsFromDDC(const FContext& Context)
{
	if (Context.Texture && Context.Resource)
	{
		for (int32 MipIndex = PendingFirstLODIdx; MipIndex < CurrentFirstLODIdx && !IsCancelled(); ++MipIndex)
		{
			const FTexture2DMipMap& MipMap = *Context.MipsView[MipIndex];
			check(MipData[MipIndex]);

			if (!MipMap.DerivedDataKey.IsEmpty())
			{
				// The overhead of doing 2 copy of each mip data (from GetSynchronous() and FMemoryReader) in hidden by other texture DDC ops happening at the same time.
				TArray<uint8> DerivedMipData;
				bool bDDCValid = true;

				const uint32 Handle = DDCHandles[MipIndex];
				if (Handle)
				{
					bDDCValid = GetDerivedDataCacheRef().GetAsynchronousResults(Handle, DerivedMipData);
					DDCHandles[MipIndex] = 0;
				}
				else
				{
					bDDCValid = GetDerivedDataCacheRef().GetSynchronous(*MipMap.DerivedDataKey, DerivedMipData, Context.Texture->GetPathName());
				}

				if (bDDCValid)				
				{
					const int32 ExpectedMipSize = CalcTextureMipMapSize(MipMap.SizeX, MipMap.SizeY, Context.Resource->GetPixelFormat(), 0);
					FMemoryReader Ar(DerivedMipData, true);

					int32 MipSize = 0;
					Ar << MipSize;

					if (MipSize == ExpectedMipSize)
					{
						Ar.Serialize(MipData[MipIndex], MipSize);
					}
					else
					{
						UE_LOG(LogTexture, Error, TEXT("DDC mip size (%d) not as expected."), MipIndex);
						MarkAsCancelled();
					}
				}
				else
				{
					// UE_LOG(LogTexture, Warning, TEXT("Failed to stream mip data from the derived data cache for %s. Streaming mips will be recached."), Context.Texture->GetPathName() );
					MarkAsCancelled();
				}
			}
			else
			{
				UE_LOG(LogTexture, Error, TEXT("DDC key missing."));
				MarkAsCancelled();
			}
		}
		FPlatformMisc::MemoryBarrier();
	}
}

#endif