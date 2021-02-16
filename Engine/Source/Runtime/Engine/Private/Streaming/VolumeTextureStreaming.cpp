// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
VolumeTextureStreaming.cpp: Helpers to stream in and out volume texture LODs.
=============================================================================*/

#include "Streaming/VolumeTextureStreaming.h"

extern RHI_API bool GUseTexture3DBulkDataRHI;

//*****************************************************************************
//***************************** Global Definitions ****************************
//*****************************************************************************

void RHICopySharedMips(FRHICommandList& RHICmdList, FRHITexture3D* DestTexture, FRHITexture3D* SrcTexture)
{
	// Transition to copy source and dest
	{
		FRHITransitionInfo TransitionsBefore[] = { FRHITransitionInfo(SrcTexture, ERHIAccess::SRVMask, ERHIAccess::CopySrc), FRHITransitionInfo(DestTexture, ERHIAccess::SRVMask, ERHIAccess::CopyDest) };
		RHICmdList.Transition(MakeArrayView(TransitionsBefore, UE_ARRAY_COUNT(TransitionsBefore)));
	}

	// Copy 
	{
		FRHICopyTextureInfo CopyInfo;

		auto SetCopyInfo = [&](FRHITexture3D* Texture3D)
		{
			CopyInfo.Size.X = Texture3D->GetSizeX();
			CopyInfo.Size.Y = Texture3D->GetSizeY();
			CopyInfo.Size.Z = Texture3D->GetSizeZ();
			CopyInfo.NumMips = Texture3D->GetNumMips();
		};

		if (DestTexture->GetNumMips() < SrcTexture->GetNumMips())
		{
			SetCopyInfo(DestTexture);
		}
		else
		{
			SetCopyInfo(SrcTexture);
		}

		CopyInfo.SourceMipIndex = SrcTexture->GetNumMips() - CopyInfo.NumMips;
		CopyInfo.DestMipIndex = DestTexture->GetNumMips() - CopyInfo.NumMips;
		RHICmdList.CopyTexture(SrcTexture, DestTexture, CopyInfo);
	}

	// Transition to SRV
	{
		FRHITransitionInfo TransitionsAfter[] = { FRHITransitionInfo(SrcTexture, ERHIAccess::CopySrc, ERHIAccess::SRVMask), FRHITransitionInfo(DestTexture, ERHIAccess::CopyDest, ERHIAccess::SRVMask) };
		RHICmdList.Transition(MakeArrayView(TransitionsAfter, UE_ARRAY_COUNT(TransitionsAfter)));
	}
}

//*****************************************************************************
//******************** FVolumeTextureMipAllocator_Reallocate ******************
//*****************************************************************************

FVolumeTextureMipAllocator_Reallocate::FVolumeTextureMipAllocator_Reallocate(UTexture* Texture)
	: FTextureMipAllocator(Texture, ETickState::AllocateMips, ETickThread::Async)
	, StreamedInMipData(PendingFirstLODIdx)
{
}

FVolumeTextureMipAllocator_Reallocate::~FVolumeTextureMipAllocator_Reallocate()
{
}

bool FVolumeTextureMipAllocator_Reallocate::AllocateMips(const FTextureUpdateContext& Context, FTextureMipInfoArray& OutMipInfos, const FTextureUpdateSyncOptions& SyncOptions)
{
	OutMipInfos.AddDefaulted(CurrentFirstLODIdx);

	// Allocate the mip memory as temporary buffers so that the FTextureMipDataProvider implementation can write to it.
	for (int32 MipIdx = PendingFirstLODIdx; MipIdx < CurrentFirstLODIdx; ++MipIdx)
	{
		const FTexture2DMipMap& OwnerMip = *Context.MipsView[MipIdx];
		FTextureMipInfo& MipInfo = OutMipInfos[MipIdx];

		// Note that streamed in mips size will always be at least as big as block size.
		MipInfo.Format = Context.Resource->GetPixelFormat();
		MipInfo.SizeX = OwnerMip.SizeX;
		MipInfo.SizeY = OwnerMip.SizeY;
		MipInfo.SizeZ = OwnerMip.SizeZ;

		uint32 TextureAlign = 0;
		MipInfo.DataSize = RHICalcTexture3DPlatformSize(MipInfo.SizeX, MipInfo.SizeY, MipInfo.SizeZ, MipInfo.Format, 1, Context.Resource->GetCreationFlags(), FRHIResourceCreateInfo(Context.Resource->GetExtData()), TextureAlign);
		StreamedInMipData.GetMipSize()[MipIdx] = MipInfo.DataSize;

		// When initializing the texture with it's bulk data, we proceed with a single allocation.
		if (!GUseTexture3DBulkDataRHI)
		{
			MipInfo.DestData = FMemory::Malloc(MipInfo.DataSize, FVolumeTextureBulkData::MALLOC_ALIGNMENT);
			StreamedInMipData.GetMipData()[MipIdx] = MipInfo.DestData;
		}
	}

	if (GUseTexture3DBulkDataRHI)
	{
		StreamedInMipData.MergeMips(ResourceState.MaxNumLODs);

		uint8* MergedMipData = (uint8*)StreamedInMipData.GetResourceBulkData();
		for (int32 MipIdx = PendingFirstLODIdx; MipIdx < CurrentFirstLODIdx; ++MipIdx)
		{
			FTextureMipInfo& MipInfo = OutMipInfos[MipIdx];
			MipInfo.DestData = MergedMipData;
			MergedMipData += MipInfo.DataSize;
		}
	}

	AdvanceTo(ETickState::FinalizeMips, ETickThread::Render);
	return true;
}

bool FVolumeTextureMipAllocator_Reallocate::FinalizeMips(const FTextureUpdateContext& Context, const FTextureUpdateSyncOptions& SyncOptions)
{
	EPixelFormat PixelFormat = Context.Resource->GetPixelFormat();

	// Create new Texture.
	{
		FRHIResourceCreateInfo CreateInfo(TEXT("FinalizeMips"));
		if (GUseTexture3DBulkDataRHI)
		{
			CreateInfo.BulkData = &StreamedInMipData;
		}

		const FTexture2DMipMap& FirstMip = *Context.MipsView[PendingFirstLODIdx];
		CreateInfo.ExtData = Context.Resource->GetExtData();
		IntermediateTextureRHI = RHICreateTexture3D(FirstMip.SizeX, FirstMip.SizeY, FirstMip.SizeZ, PixelFormat, ResourceState.NumRequestedLODs, Context.Resource->GetCreationFlags(), CreateInfo);
	}

	// Copy shared mips.
	{
		bool bCopySharedMipsDone = false;
		ENQUEUE_RENDER_COMMAND(FCopySharedMipsForTexture3D)(
			[&](FRHICommandListImmediate& RHICmdList)
		{
			RHICopySharedMips(RHICmdList, IntermediateTextureRHI.GetReference(), Context.Resource->GetTexture3DRHI());
			bCopySharedMipsDone = true;
		});
		// Expected to execute immediately since ran on the renderthread.
		check(bCopySharedMipsDone);
	}

	// Update the streamed in mips if they were not initialized from the bulk data.
	if (!GUseTexture3DBulkDataRHI)
	{
		const int32 BlockSizeX = GPixelFormats[PixelFormat].BlockSizeX;
		const int32 BlockSizeY = GPixelFormats[PixelFormat].BlockSizeY;
		const int32 BlockBytes = GPixelFormats[PixelFormat].BlockBytes;
		ensure(GPixelFormats[PixelFormat].BlockSizeZ == 1);

		for (int32 MipIdx = PendingFirstLODIdx; MipIdx < CurrentFirstLODIdx; ++MipIdx)
		{
			const FTexture2DMipMap& Mip = *Context.MipsView[MipIdx];
			const uint8* MipData = (uint8*)StreamedInMipData.GetMipData()[MipIdx];
			if (MipData)
			{
				FUpdateTextureRegion3D UpdateRegion(0, 0, 0, 0, 0, 0, Mip.SizeX, Mip.SizeY, Mip.SizeZ);
				const uint32 NumBlockX = (uint32)FMath::DivideAndRoundUp<int32>(Mip.SizeX, BlockSizeX);
				const uint32 NumBlockY = (uint32)FMath::DivideAndRoundUp<int32>(Mip.SizeY, BlockSizeY);
				RHIUpdateTexture3D(IntermediateTextureRHI, MipIdx - PendingFirstLODIdx, UpdateRegion, NumBlockX * BlockBytes, NumBlockX * NumBlockY * BlockBytes, MipData);
			}
		}
	}
	StreamedInMipData.Discard();

	// Use the new texture resource for the texture asset, must run on the renderthread.
	Context.Resource->FinalizeStreaming(IntermediateTextureRHI);
	// No need for the intermediate texture anymore.
	IntermediateTextureRHI.SafeRelease();

	// Update is complete, nothing more to do.
	AdvanceTo(ETickState::Done, ETickThread::None);
	return true;
}

void FVolumeTextureMipAllocator_Reallocate::Cancel(const FTextureUpdateSyncOptions& SyncOptions)
{
	// Release the intermediate texture. If not null, this will be on the renderthread.
	IntermediateTextureRHI.SafeRelease();
	// Release the temporary mip data. Can be run on either renderthread or async threads.
	StreamedInMipData.Discard();
}

FTextureMipAllocator::ETickThread FVolumeTextureMipAllocator_Reallocate::GetCancelThread() const
{
	// If there is an  intermediate texture, it is safer to released on the renderthread.
	if (IntermediateTextureRHI)
	{
		return ETickThread::Render;
	}
	// Otherwise, if there are only temporary mip data, they can be freed on any threads.
	else if (StreamedInMipData.GetResourceBulkData())
	{
		return ETickThread::Async;
	}
	// Nothing to do.
	else
	{
		return ETickThread::None;
	}
}
