// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
Texture2DMipDataProvider_DDC.cpp : Implementation of FTextureMipDataProvider using DDC requests.
=============================================================================*/

#include "Texture2DMipDataProvider_DDC.h"
#include "DerivedDataCacheInterface.h"
#include "Engine/Texture2D.h"
#include "TextureResource.h"
#include "Streaming/Texture2DStreamIn_DDC.h" // for FAbandonedDDCHandleManager

#if WITH_EDITORONLY_DATA

FTexture2DMipDataProvider_DDC::FTexture2DMipDataProvider_DDC()
	: FTextureMipDataProvider(ETickState::Init, ETickThread::Async)
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
		UTexture2D* Texture2D = CastChecked<UTexture2D>(Context.Texture, ECastCheckedType::NullChecked);
		const TIndirectArray<FTexture2DMipMap>& OwnerMips = Texture2D->GetPlatformMips();
		DDCHandles.AddZeroed(Context.CurrentFirstMipIndex);

		for (int32 MipIndex = Context.PendingFirstMipIndex; MipIndex < Context.CurrentFirstMipIndex; ++MipIndex)
		{
			const FTexture2DMipMap& OwnerMip = OwnerMips[MipIndex];
			if (!OwnerMip.DerivedDataKey.IsEmpty())
			{
				DDCHandles[MipIndex] = GetDerivedDataCacheRef().GetAsynchronous(*OwnerMip.DerivedDataKey);
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
	for (int32 MipIndex = StartingMipIndex; MipIndex < Context.CurrentFirstMipIndex; ++MipIndex)
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

				if (MipSize == MipInfo.DataSize)
				{
					Ar.Serialize(MipInfo.DestData, MipSize);
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

	AdvanceTo(ETickState::CleanUp, ETickThread::Async);
	return Context.CurrentFirstMipIndex;
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
