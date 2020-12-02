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

FTexture2DMipDataProvider_DDC::FTexture2DMipDataProvider_DDC(const UTexture* Texture)
	: FTextureMipDataProvider(Texture, ETickState::Init, ETickThread::Async)
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

		for (int32 MipIndex = PendingFirstLODIdx; MipIndex < CurrentFirstLODIdx; ++MipIndex)
		{
			const FTexture2DMipMap& OwnerMip = *Context.MipsView[MipIndex];
			if (!OwnerMip.DerivedDataKey.IsEmpty())
			{
				DDCHandles[MipIndex] = GetDerivedDataCacheRef().GetAsynchronous(*OwnerMip.DerivedDataKey, Context.Texture->GetPathName());
			}
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
		AdvanceTo(ETickState::GetMips, ETickThread::Async);
	}
}

int32 FTexture2DMipDataProvider_DDC::GetMips(
	const FTextureUpdateContext& Context,
	int32 StartingMipIndex,
	const FTextureMipInfoArray& MipInfos, 
	const FTextureUpdateSyncOptions& SyncOptions)
{
	for (int32 MipIndex = StartingMipIndex; MipIndex < CurrentFirstLODIdx; ++MipIndex)
	{
		const uint32 Handle = DDCHandles[MipIndex];
		bool bSuccess = false;
		if (Handle)
		{
			DDCHandles[MipIndex] = 0; // Clear the handle.

			TArray<uint8> DerivedMipData;
			if (GetDerivedDataCacheRef().GetAsynchronousResults(Handle, DerivedMipData))
			{
				const FTextureMipInfo& MipInfo = MipInfos[MipIndex];

				// The result must be read from a memory reader!
				FMemoryReader Ar(DerivedMipData, true);
				int32 MipSize = 0;
				Ar << MipSize;

				const uint32 DepthOrArraySize = FMath::Max<uint32>(MipInfo.ArraySize, MipInfo.SizeZ);
				if (MipSize == MipInfo.DataSize)
				{
					Ar.Serialize(MipInfo.DestData, MipSize);
					bSuccess = true;
				}
				else if (MipSize < MipInfo.DataSize && DepthOrArraySize > 1 && MipInfo.DataSize % DepthOrArraySize == 0 && MipSize % DepthOrArraySize == 0)
				{
					UE_LOG(LogTexture, Verbose, TEXT("DDC mip size smaller than streaming buffer size. (%s, Mip %d): %d KB / %d KB."), *Context.Resource->GetTextureName().ToString(), ResourceState.MaxNumLODs - MipIndex, MipInfo.DataSize / 1024, MipSize / 1024);

					const uint64 SourceSubSize = MipSize / DepthOrArraySize;
					const uint64 DestSubSize = MipInfo.DataSize / DepthOrArraySize;
					const uint64 PaddingSubSize = DestSubSize - SourceSubSize;

					uint8* DestData = (uint8*)MipInfo.DestData;
					for (uint32 SubIdx = 0; SubIdx < DepthOrArraySize; ++SubIdx)
					{
						Ar.Serialize(DestData, SourceSubSize);
						DestData += SourceSubSize;
						FMemory::Memzero(DestData, PaddingSubSize);
						DestData += PaddingSubSize;
					}
					bSuccess = true;
				}
				else
				{
					UE_LOG(LogTexture, Warning, TEXT("Mismatch between DDC mip size and streaming buffer size. (%s, Mip %d): %d KB / %d KB."), *Context.Resource->GetTextureName().ToString(), ResourceState.MaxNumLODs - MipIndex, MipInfo.DataSize / 1024, MipSize / 1024);
					FMemory::Memzero(MipInfo.DestData, MipInfo.DataSize);
				}
			}
		}

		if (!bSuccess)
		{
			AdvanceTo(ETickState::CleanUp, ETickThread::Async);
			return MipIndex; // We failed at getting this mip. Cancel will be called.
		}
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
	ReleaseDDCHandles();
	AdvanceTo(ETickState::Done, ETickThread::None);
}

void FTexture2DMipDataProvider_DDC::Cancel(const FTextureUpdateSyncOptions& SyncOptions)
{
	ReleaseDDCHandles();
}

FTextureMipDataProvider::ETickThread FTexture2DMipDataProvider_DDC::GetCancelThread() const
{
	if (DDCHandles.Num())
	{
		return ETickThread::Async;
	}
	else
	{
		return ETickThread::None;
	}
}

void FTexture2DMipDataProvider_DDC::ReleaseDDCHandles()
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
}

#endif //WITH_EDITORONLY_DATA
