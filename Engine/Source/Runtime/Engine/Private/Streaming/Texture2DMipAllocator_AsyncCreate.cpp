// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
Texture2DStreamIn_AsyncCreate.cpp: Implementation of FTextureMipAllocator using RHIAsyncCreateTexture2D
=============================================================================*/

#include "Texture2DMipAllocator_AsyncCreate.h"
#include "RenderUtils.h"
#include "Containers/ResourceArray.h"

FTexture2DMipAllocator_AsyncCreate::FTexture2DMipAllocator_AsyncCreate()
	: FTextureMipAllocator(ETickState::AllocateMips, ETickThread::Async)
{
}

FTexture2DMipAllocator_AsyncCreate::~FTexture2DMipAllocator_AsyncCreate()
{
	check(!FinalMipData.Num());
}

bool FTexture2DMipAllocator_AsyncCreate::AllocateMips(
	const FTextureUpdateContext& Context, 
	FTextureMipInfoArray& OutMipInfos, 
	const FTextureUpdateSyncOptions& SyncOptions)
{
	check(Context.PendingFirstMipIndex < Context.CurrentFirstMipIndex);

	UTexture2D* Texture2D = CastChecked<UTexture2D>(Context.Texture, ECastCheckedType::NullChecked);
	FTexture2DResource* Resource = static_cast<FTexture2DResource*>(Texture2D->Resource);
	FTexture2DRHIRef Texture2DRHI = Resource ? Resource->GetTexture2DRHI() : FTexture2DRHIRef();
	if (!Texture2DRHI)
	{
		return false;
	}

	OutMipInfos.AddDefaulted(Context.CurrentFirstMipIndex);

	// Allocate the mip memory as temporary buffers so that the FTextureMipDataProvider implementation can write to it.
	const TIndirectArray<FTexture2DMipMap>& OwnerMips = Texture2D->GetPlatformMips();
	for (int32 MipIndex = Context.PendingFirstMipIndex; MipIndex < Context.CurrentFirstMipIndex; ++MipIndex)
	{
		const FTexture2DMipMap& OwnerMip = OwnerMips[MipIndex];
		FTextureMipInfo& MipInfo = OutMipInfos[MipIndex];

		MipInfo.Format = Texture2DRHI->GetFormat();
		MipInfo.SizeX = OwnerMip.SizeX;
		MipInfo.SizeY = OwnerMip.SizeY;
		MipInfo.DataSize = CalcTextureMipMapSize(MipInfo.SizeX, MipInfo.SizeY, MipInfo.Format, 0);
		// Allocate the mip in main memory. It will later be used to create the mips with proper initial states (without going through lock/unlock).
		MipInfo.DestData = FMemory::Malloc(MipInfo.DataSize);

		// Backup the allocated memory so that it can safely be freed.
		FinalMipData.Add(MipInfo.DestData);
	}

	// Backup size and format.
	if (OutMipInfos.IsValidIndex(Context.PendingFirstMipIndex))
	{
		FinalSizeX = OutMipInfos[Context.PendingFirstMipIndex].SizeX;
		FinalSizeY = OutMipInfos[Context.PendingFirstMipIndex].SizeY;
		FinalFormat = OutMipInfos[Context.PendingFirstMipIndex].Format;

		// Once the FTextureMipDataProvider has set the mip data, FinalizeMips can then create the texture in it's step (1).
		AdvanceTo(ETickState::FinalizeMips, ETickThread::Async);
		return true;
	}
	else // No new mips? something is wrong.
	{
		return false;
	}
}

// This gets called 2 times :
// - Async : create the texture with the mips data
// - Render : swap the results
bool FTexture2DMipAllocator_AsyncCreate::FinalizeMips(const FTextureUpdateContext& Context, const FTextureUpdateSyncOptions& SyncOptions)
{
	UTexture2D* Texture2D = CastChecked<UTexture2D>(Context.Texture, ECastCheckedType::NullChecked);
	FTexture2DResource* Resource = static_cast<FTexture2DResource*>(Texture2D->Resource);
	FTexture2DRHIRef Texture2DRHI = Resource ? Resource->GetTexture2DRHI() : FTexture2DRHIRef();
	if (!Texture2DRHI)
	{
		return false;
	}

	// Step (1) : Create the texture on the async thread, having the new mip data as reference so that it can be initialized correctly.
	if (!IntermediateTextureRHI)
	{
		const uint32 Flags = (Texture2D->SRGB ? TexCreate_SRGB : 0) | TexCreate_DisableAutoDefrag;

		// Create the intermediate texture.
		IntermediateTextureRHI = RHIAsyncCreateTexture2D(FinalSizeX, FinalSizeY, FinalFormat, Context.NumRequestedMips, Flags, FinalMipData.GetData(), FinalMipData.Num());
		// Free the temporary mip data, since copy is now in the RHIAsyncCreateTexture2D command.
		ReleaseAllocatedMipData();

		// Go to next step, on the renderthread.
		AdvanceTo(ETickState::FinalizeMips, ETickThread::Render);
	}
	// Step (2) : Copy the non initialized mips on the using RHICopySharedMips, must run on the renderthread.
	else
	{
		// Copy the mips.
		RHICopySharedMips(IntermediateTextureRHI, Texture2DRHI);
		// Use the new texture resource for the texture asset, must run on the renderthread.
		Resource->UpdateTexture(IntermediateTextureRHI, Context.PendingFirstMipIndex);
		// No need for the intermediate texture anymore.
		IntermediateTextureRHI.SafeRelease();

		// Update complete, nothing more to do.
		AdvanceTo(ETickState::Done, ETickThread::None);
	}
	return true;
}

void FTexture2DMipAllocator_AsyncCreate::Cancel(const FTextureUpdateSyncOptions& SyncOptions)
{
	// Release the intermediate texture. If not null, this will be on the renderthread.
	IntermediateTextureRHI.SafeRelease();
	// Release the temporary mip data. Can be run on either renderthread or async threads.
	ReleaseAllocatedMipData();
}

FTextureMipAllocator::ETickThread FTexture2DMipAllocator_AsyncCreate::GetCancelThread() const
{
	// If there is an  intermediate texture, it is safer to released on the renderthread.
	if (IntermediateTextureRHI)
	{
		return ETickThread::Render;
	}
	// Otherwise, if there are only temporary mip data, they can be freed on any threads.
	else if (FinalMipData.Num())
	{
		return ETickThread::Async;
	}
	// Nothing to do.
	else
	{
		return ETickThread::None;
	}
}

int32 FTexture2DMipAllocator_AsyncCreate::GetCurrentFirstMip(UTexture* Texture) const
{
	UTexture2D* Texture2D = CastChecked<UTexture2D>(Texture, ECastCheckedType::NullChecked);
	FTexture2DResource* Resource = static_cast<FTexture2DResource*>(Texture2D->Resource);
	return Resource ? Resource->GetCurrentFirstMip() : INDEX_NONE;
}


void FTexture2DMipAllocator_AsyncCreate::ReleaseAllocatedMipData()
{
	// Release the temporary mip data.
	for (void* NewData : FinalMipData)
	{
		if (NewData)
		{
			FMemory::Free(NewData);
		}
	}
	FinalMipData.Empty();
}
