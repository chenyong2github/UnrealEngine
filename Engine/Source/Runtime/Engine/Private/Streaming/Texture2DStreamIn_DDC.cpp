// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
Texture2DStreamIn_DDC.cpp: Stream in helper for 2D textures loading DDC files.
=============================================================================*/

#include "Streaming/Texture2DStreamIn_DDC.h"
#include "Streaming/TextureStreamingHelpers.h"
#include "RenderUtils.h"
#include "DerivedDataCacheInterface.h"
#include "Serialization/MemoryReader.h"

#if WITH_EDITORONLY_DATA

int32 GStreamingUseAsyncRequestsForDDC = 0;
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

namespace
{
	class FAbandonedDDCHandleManager
	{
	public:
		void Add(uint32 InHandle);
		void Purge();
	private:
		TArray<uint32> Handles;	
		FCriticalSection CS;
		uint32 TotalAdd = 0;
	};


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

	FAbandonedDDCHandleManager GAbandonedDDCHandleManager;
}

void PurgeAbandonedDDCHandles()
{
	GAbandonedDDCHandleManager.Purge();
}

FTexture2DStreamIn_DDC::FTexture2DStreamIn_DDC(UTexture2D* InTexture, int32 InRequestedMips)
	: FTexture2DStreamIn(InTexture, InRequestedMips)
	, bDDCIsInvalid(false)
{
	DDCHandles.AddZeroed(InTexture->GetNumMips());
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
		const TIndirectArray<FTexture2DMipMap>& OwnerMips = Context.Texture->GetPlatformMips();
		const int32 CurrentFirstMip = Context.Resource->GetCurrentFirstMip();
		const FTexture2DRHIRef Texture2DRHI = Context.Resource->GetTexture2DRHI();

		for (int32 MipIndex = PendingFirstMip; MipIndex < CurrentFirstMip && !IsCancelled(); ++MipIndex)
		{
			const FTexture2DMipMap& MipMap = OwnerMips[MipIndex];
			if (!MipMap.DerivedDataKey.IsEmpty())
			{
				check(!DDCHandles[MipIndex]);
				DDCHandles[MipIndex] = GetDerivedDataCacheRef().GetAsynchronous(*MipMap.DerivedDataKey);

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
	if (Context.Texture && Context.Resource)
	{
		const int32 CurrentFirstMip = Context.Resource->GetCurrentFirstMip();
		for (int32 MipIndex = PendingFirstMip; MipIndex < CurrentFirstMip && !IsCancelled(); ++MipIndex)
		{
			const uint32 Handle = DDCHandles[MipIndex];
			if (Handle && !GetDerivedDataCacheRef().PollAsynchronousCompletion(Handle))
			{
				return false;
			}
		}
	}
	else
	{
		MarkAsCancelled();
	}
	return true;
}

void FTexture2DStreamIn_DDC::DoLoadNewMipsFromDDC(const FContext& Context)
{
	if (Context.Texture && Context.Resource)
	{
		const TIndirectArray<FTexture2DMipMap>& OwnerMips = Context.Texture->GetPlatformMips();
		const int32 CurrentFirstMip = Context.Resource->GetCurrentFirstMip();
		const FTexture2DRHIRef Texture2DRHI = Context.Resource->GetTexture2DRHI();

		for (int32 MipIndex = PendingFirstMip; MipIndex < CurrentFirstMip && !IsCancelled(); ++MipIndex)
		{
			const FTexture2DMipMap& MipMap = OwnerMips[MipIndex];

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
					bDDCValid = GetDerivedDataCacheRef().GetSynchronous(*MipMap.DerivedDataKey, DerivedMipData);
				}

				if (bDDCValid)				
				{
					const int32 ExpectedMipSize = CalcTextureMipMapSize(MipMap.SizeX, MipMap.SizeY, Texture2DRHI->GetFormat(), 0);
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
						bDDCIsInvalid = true;
					}
				}
				else
				{
					// UE_LOG(LogTexture, Warning, TEXT("Failed to stream mip data from the derived data cache for %s. Streaming mips will be recached."), Context.Texture->GetPathName() );
					MarkAsCancelled();
					bDDCIsInvalid = true;
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