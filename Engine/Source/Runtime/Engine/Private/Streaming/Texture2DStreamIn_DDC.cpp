// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
Texture2DStreamIn_DDC.cpp: Stream in helper for 2D textures loading DDC files.
=============================================================================*/

#include "Streaming/Texture2DStreamIn_DDC.h"
#include "Streaming/TextureStreamingHelpers.h"
#include "RenderUtils.h"
#include "Serialization/MemoryReader.h"

#if WITH_EDITORONLY_DATA

#include "DerivedDataCache.h"
#include "DerivedDataCacheInterface.h"
#include "DerivedDataCacheKey.h"
#include "DerivedDataRequestOwner.h"

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
	, DDCRequestOwner(UE::DerivedData::EPriority::Normal)
{
	DDCHandles.AddZeroed(ResourceState.MaxNumLODs);
	DDCBuffers.AddZeroed(ResourceState.MaxNumLODs);
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
	if (!Context.Texture || !Context.Resource)
	{
		return;
	}

	if (const FTexturePlatformData* PlatformData = Context.Texture->GetPlatformData())
	{
		const int32 LODBias = static_cast<int32>(Context.MipsView.GetData() - PlatformData->Mips.GetData());

		if (PlatformData->DerivedDataKey.IsType<FString>())
		{
			for (int32 MipIndex = PendingFirstLODIdx; MipIndex < CurrentFirstLODIdx && !IsCancelled(); ++MipIndex)
			{
				const FTexture2DMipMap& MipMap = *Context.MipsView[MipIndex];
				if (MipMap.IsPagedToDerivedData())
				{
					check(!DDCHandles[MipIndex]);
					DDCHandles[MipIndex] = GetDerivedDataCacheRef().GetAsynchronous(*PlatformData->GetDerivedDataMipKeyString(MipIndex + LODBias, MipMap), Context.Texture->GetPathName());

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
					UE_LOG(LogTexture, Error, TEXT("Attempting to stream in a mip that is already present."));
					MarkAsCancelled();
				}
			}
		}
		else if (PlatformData->DerivedDataKey.IsType<UE::DerivedData::FCacheKeyProxy>())
		{
			using namespace UE::DerivedData;
			TArray<FCacheGetChunkRequest> MipKeys;

			TStringBuilder<256> MipNameBuilder;
			Context.Texture->GetPathName(nullptr, MipNameBuilder);
			const int32 TextureNameLen = MipNameBuilder.Len();

			const FCacheKey& Key = *PlatformData->DerivedDataKey.Get<UE::DerivedData::FCacheKeyProxy>().AsCacheKey();
			for (int32 MipIndex = PendingFirstLODIdx; MipIndex < CurrentFirstLODIdx && !IsCancelled(); ++MipIndex)
			{
				const FTexture2DMipMap& MipMap = *Context.MipsView[MipIndex];
				if (MipMap.IsPagedToDerivedData())
				{
					FCacheGetChunkRequest& Request = MipKeys.AddDefaulted_GetRef();
					MipNameBuilder.Appendf(TEXT(" [MIP 0]"), MipIndex + LODBias);
					Request.Name = MipNameBuilder;
					Request.Key = Key;
					Request.Id = FTexturePlatformData::MakeMipId(MipIndex + LODBias);
					Request.UserData = MipIndex;
					MipNameBuilder.RemoveSuffix(MipNameBuilder.Len() - TextureNameLen);
				}
			}

			if (MipKeys.Num())
			{
				GetCache().GetChunks(MipKeys, DDCRequestOwner, [this](FCacheGetChunkResponse&& Response)
				{
					if (Response.Status == EStatus::Ok)
					{
						const int32 MipIndex = int32(Response.UserData);
						check(!DDCBuffers[MipIndex]);
						DDCBuffers[MipIndex] = MoveTemp(Response.RawData);
					}
				});
			}
		}
		else
		{
			UE_LOG(LogTexture, Error, TEXT("Attempting to stream in mips for texture that has not generated a supported derived data key format."));
			MarkAsCancelled();
		}
	}
	else
	{
		UE_LOG(LogTexture, Error, TEXT("Attempting to stream in mips for texture that has not generated derived data yet."));
		MarkAsCancelled();
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
	return DDCRequestOwner.Poll();
}

void FTexture2DStreamIn_DDC::DoLoadNewMipsFromDDC(const FContext& Context)
{
	if (!Context.Texture || !Context.Resource)
	{
		return;
	}

	if (const FTexturePlatformData* PlatformData = Context.Texture->GetPlatformData())
	{
		const int32 LODBias = static_cast<int32>(Context.MipsView.GetData() - PlatformData->Mips.GetData());

		if (PlatformData->DerivedDataKey.IsType<FString>())
		{
			for (int32 MipIndex = PendingFirstLODIdx; MipIndex < CurrentFirstLODIdx && !IsCancelled(); ++MipIndex)
			{
				const FTexture2DMipMap& MipMap = *Context.MipsView[MipIndex];
				check(MipData[MipIndex]);

				if (MipMap.IsPagedToDerivedData())
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
						bDDCValid = GetDerivedDataCacheRef().GetSynchronous(*PlatformData->GetDerivedDataMipKeyString(MipIndex + LODBias, MipMap), DerivedMipData, Context.Texture->GetPathName());
					}

					if (bDDCValid)
					{
						const int32 ExpectedMipSize = CalcTextureMipMapSize(MipMap.SizeX, MipMap.SizeY, Context.Resource->GetPixelFormat(), 0);
						FMemoryReader Ar(DerivedMipData, true);

						if (DerivedMipData.Num() == ExpectedMipSize)
						{
							Ar.Serialize(MipData[MipIndex], DerivedMipData.Num());
						}
						else
						{
							UE_LOG(LogTexture, Error, TEXT("DDC mip size (%d) not as expected (%d)."), MipIndex, ExpectedMipSize);
							MarkAsCancelled();
						}
					}
					else
					{
						MarkAsCancelled();
					}
				}
				else
				{
					UE_LOG(LogTexture, Error, TEXT("Attempting to stream in a mip that is already present."));
					MarkAsCancelled();
				}
			}
		}
		else if (PlatformData->DerivedDataKey.IsType<UE::DerivedData::FCacheKeyProxy>())
		{
			for (int32 MipIndex = PendingFirstLODIdx; MipIndex < CurrentFirstLODIdx && !IsCancelled(); ++MipIndex)
			{
				const FTexture2DMipMap& MipMap = *Context.MipsView[MipIndex];
				check(MipData[MipIndex]);

				if (MipMap.IsPagedToDerivedData())
				{
					if (DDCBuffers[MipIndex])
					{
						const int32 ExpectedMipSize = CalcTextureMipMapSize(MipMap.SizeX, MipMap.SizeY, Context.Resource->GetPixelFormat(), 0);
						if (DDCBuffers[MipIndex].GetSize() == ExpectedMipSize)
						{
							FMemory::Memcpy(MipData[MipIndex], DDCBuffers[MipIndex].GetData(), DDCBuffers[MipIndex].GetSize());
						}
						else
						{
							UE_LOG(LogTexture, Error, TEXT("DDC mip size (%d) not as expected (%d) for mip %d of %s."), static_cast<int32>(DDCBuffers[MipIndex].GetSize()), ExpectedMipSize, MipIndex, *Context.Texture->GetPathName());
							MarkAsCancelled();
						}
						DDCBuffers[MipIndex].Reset();
					}
					else
					{
						MarkAsCancelled();
					}
				}
				else
				{
					UE_LOG(LogTexture, Error, TEXT("Attempting to stream in a mip that is already present."));
					MarkAsCancelled();
				}
			}
		}
		else
		{
			UE_LOG(LogTexture, Error, TEXT("Attempting to stream in mips for texture that has not generated a supported derived data key format."));
			MarkAsCancelled();
		}
		FPlatformMisc::MemoryBarrier();
	}
	else
	{
		UE_LOG(LogTexture, Error, TEXT("Attempting to stream in mips for texture that has not generated derived data yet."));
		MarkAsCancelled();
	}
}

#endif