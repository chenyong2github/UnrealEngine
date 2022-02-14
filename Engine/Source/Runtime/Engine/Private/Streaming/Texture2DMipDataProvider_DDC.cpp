// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
Texture2DMipDataProvider_DDC.cpp : Implementation of FTextureMipDataProvider using DDC requests.
=============================================================================*/

#include "Texture2DMipDataProvider_DDC.h"
#include "DerivedDataCacheInterface.h"
#include "Engine/Texture.h"
#include "TextureResource.h"
#include "Streaming/Texture2DStreamIn_DDC.h" // for FAbandonedDDCHandleManager
#include "Streaming/TextureStreamingHelpers.h"

#if WITH_EDITORONLY_DATA

#include "DerivedDataCache.h"
#include "DerivedDataCacheInterface.h"
#include "DerivedDataCacheKey.h"

FTexture2DMipDataProvider_DDC::FTexture2DMipDataProvider_DDC(const UTexture* Texture)
	: FTextureMipDataProvider(Texture, ETickState::Init, ETickThread::Async)
	, DDCRequestOwner(UE::DerivedData::EPriority::Normal)
{
}

FTexture2DMipDataProvider_DDC::~FTexture2DMipDataProvider_DDC()
{
	check(!DDCHandles.Num());
}

void FTexture2DMipDataProvider_DDC::Init(const FTextureUpdateContext& Context, const FTextureUpdateSyncOptions& SyncOptions)
{
	if (!DDCHandles.Num())
	{
		DDCHandles.AddZeroed(CurrentFirstLODIdx);
		DDCBuffers.AddZeroed(CurrentFirstLODIdx);

		FTexturePlatformData*const * PtrPlatformData = const_cast<UTexture*>(Context.Texture)->GetRunningPlatformData();
		if (PtrPlatformData && *PtrPlatformData)
		{
			const FTexturePlatformData* PlatformData = *PtrPlatformData;
			const int32 LODBias = static_cast<int32>(Context.MipsView.GetData() - PlatformData->Mips.GetData());

			if (PlatformData->DerivedDataKey.IsType<FString>())
			{
				for (int32 MipIndex = PendingFirstLODIdx; MipIndex < CurrentFirstLODIdx; ++MipIndex)
				{
					const FTexture2DMipMap& OwnerMip = *Context.MipsView[MipIndex];
					if (OwnerMip.IsPagedToDerivedData())
					{
						DDCHandles[MipIndex] = GetDerivedDataCacheRef().GetAsynchronous(*PlatformData->GetDerivedDataMipKeyString(MipIndex + LODBias, OwnerMip), Context.Texture->GetPathName());
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
				for (int32 MipIndex = PendingFirstLODIdx; MipIndex < CurrentFirstLODIdx; ++MipIndex)
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
			}
		}
		else
		{
			UE_LOG(LogTexture, Error, TEXT("Attempting to stream in mips for texture that has not generated derived data yet."));
		}


		*SyncOptions.bSnooze = true;
	}
	else // The DDC request have been issued, only check whether they are ready (since no good sync option is used).
	{
		for (uint32 Handle : DDCHandles)
		{
			if (Handle && !GetDerivedDataCacheRef().PollAsynchronousCompletion(Handle))
			{
				*SyncOptions.bSnooze = true;
				return;
			}
		}

		if (!DDCRequestOwner.Poll())
		{
			*SyncOptions.bSnooze = true;
			return;
		}

		AdvanceTo(ETickState::GetMips, ETickThread::Async);
	}
}

bool FTexture2DMipDataProvider_DDC::SerializeMipInfo(const FTextureUpdateContext& Context, FArchive& Ar, int32 MipIndex, int64 MipSize, const FTextureMipInfo& OutMipInfo)
{
	const uint32 DepthOrArraySize = FMath::Max<uint32>(OutMipInfo.ArraySize, OutMipInfo.SizeZ);
	if (MipSize == OutMipInfo.DataSize)
	{
		Ar.Serialize(OutMipInfo.DestData, MipSize);
		return true;
	}
	else if (uint64(MipSize) < OutMipInfo.DataSize && DepthOrArraySize > 1 && OutMipInfo.DataSize % DepthOrArraySize == 0 && MipSize % DepthOrArraySize == 0)
	{
		UE_LOG(LogTexture, Verbose, TEXT("DDC mip size smaller than streaming buffer size. (%s, Mip %d): %d KB / %d KB."), *Context.Resource->GetTextureName().ToString(), ResourceState.MaxNumLODs - MipIndex, OutMipInfo.DataSize / 1024, MipSize / 1024);

		const uint64 SourceSubSize = MipSize / DepthOrArraySize;
		const uint64 DestSubSize = OutMipInfo.DataSize / DepthOrArraySize;
		const uint64 PaddingSubSize = DestSubSize - SourceSubSize;

		uint8* DestData = (uint8*)OutMipInfo.DestData;
		for (uint32 SubIdx = 0; SubIdx < DepthOrArraySize; ++SubIdx)
		{
			Ar.Serialize(DestData, SourceSubSize);
			DestData += SourceSubSize;
			FMemory::Memzero(DestData, PaddingSubSize);
			DestData += PaddingSubSize;
		}
		return true;
	}

	UE_LOG(LogTexture, Warning, TEXT("Mismatch between DDC mip size and streaming buffer size. (%s, Mip %d): %d KB / %d KB."), *Context.Resource->GetTextureName().ToString(), ResourceState.MaxNumLODs - MipIndex, OutMipInfo.DataSize / 1024, MipSize / 1024);
	FMemory::Memzero(OutMipInfo.DestData, OutMipInfo.DataSize);
	return false;
}

int32 FTexture2DMipDataProvider_DDC::GetMips(
	const FTextureUpdateContext& Context,
	int32 StartingMipIndex,
	const FTextureMipInfoArray& MipInfos, 
	const FTextureUpdateSyncOptions& SyncOptions)
{
	FTexturePlatformData* const* PtrPlatformData = const_cast<UTexture*>(Context.Texture)->GetRunningPlatformData();
	if (PtrPlatformData && *PtrPlatformData)
	{
		const FTexturePlatformData* PlatformData = *PtrPlatformData;
		const int32 LODBias = static_cast<int32>(Context.MipsView.GetData() - PlatformData->Mips.GetData());

		if (PlatformData->DerivedDataKey.IsType<FString>())
		{
			for (int32 MipIndex = StartingMipIndex; MipIndex < CurrentFirstLODIdx; ++MipIndex)
			{
				const uint32 Handle = DDCHandles[MipIndex];
				bool bSuccess = false;
				if (Handle)
				{
					DDCHandles[MipIndex] = 0; // Clear the handle.

					TArray64<uint8> DerivedMipData;
					if (GetDerivedDataCacheRef().GetAsynchronousResults(Handle, DerivedMipData))
					{
						const FTextureMipInfo& MipInfo = MipInfos[MipIndex];

						// The result must be read from a memory reader!
						FMemoryReaderView Ar(MakeMemoryView(DerivedMipData), true);
						if (SerializeMipInfo(Context, Ar, MipIndex, DerivedMipData.Num(), MipInfo))
						{
							bSuccess = true;
						}
					}
				}

				if (!bSuccess)
				{
					AdvanceTo(ETickState::CleanUp, ETickThread::Async);
					return MipIndex; // We failed at getting this mip. Cancel will be called.
				}
			}
		}
		else if (PlatformData->DerivedDataKey.IsType<UE::DerivedData::FCacheKeyProxy>())
		{
			for (int32 MipIndex = StartingMipIndex; MipIndex < CurrentFirstLODIdx; ++MipIndex)
			{
				bool bSuccess = false;
				if (DDCBuffers[MipIndex])
				{
					// The result must be read from a memory reader!
					FMemoryReaderView Ar(DDCBuffers[MipIndex], true);
					if (SerializeMipInfo(Context, Ar, MipIndex, DDCBuffers[MipIndex].GetSize(), MipInfos[MipIndex]))
					{
						bSuccess = true;
					}
					DDCBuffers[MipIndex].Reset();
				}

				if (!bSuccess)
				{
					AdvanceTo(ETickState::CleanUp, ETickThread::Async);
					return MipIndex; // We failed at getting this mip. Cancel will be called.
				}
			}
		}
		else
		{
			UE_LOG(LogTexture, Error, TEXT("Attempting to stream in mips for texture that has not generated a supported derived data key format."));
		}
	}
	else
	{
		UE_LOG(LogTexture, Error, TEXT("Attempting to stream in mips for texture that has not generated derived data yet."));
	}

	AdvanceTo(ETickState::CleanUp, ETickThread::Async);
	return CurrentFirstLODIdx;
}

bool FTexture2DMipDataProvider_DDC::PollMips(const FTextureUpdateSyncOptions& SyncOptions)
{
	AdvanceTo(ETickState::CleanUp, ETickThread::Async);
	return true;
}

void FTexture2DMipDataProvider_DDC::CleanUp(const FTextureUpdateSyncOptions& SyncOptions)
{
	ReleaseDDCResources();
	AdvanceTo(ETickState::Done, ETickThread::None);
}

void FTexture2DMipDataProvider_DDC::Cancel(const FTextureUpdateSyncOptions& SyncOptions)
{
	ReleaseDDCResources();
}

FTextureMipDataProvider::ETickThread FTexture2DMipDataProvider_DDC::GetCancelThread() const
{
	if (DDCHandles.Num() || DDCBuffers.Num())
	{
		return ETickThread::Async;
	}
	else
	{
		return ETickThread::None;
	}
}

void FTexture2DMipDataProvider_DDC::ReleaseDDCResources()
{
	for (uint32& Handle : DDCHandles)
	{
		if (Handle)
		{
			GAbandonedDDCHandleManager.Add(Handle);
			Handle = 0;
		}
	}
	DDCHandles.Empty();
	DDCRequestOwner.Cancel();
	DDCBuffers.Empty();
}

#endif //WITH_EDITORONLY_DATA
