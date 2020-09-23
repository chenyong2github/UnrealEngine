// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	Texture3DResource.cpp: Implementation of FTexture3DResource used  by streamable UVolumeTexture.
=============================================================================*/

#include "Rendering/Texture3DResource.h"
#include "Engine/VolumeTexture.h"
#include "RenderUtils.h"

//*****************************************************************************

extern RHI_API bool GUseTexture3DBulkDataRHI;

//*****************************************************************************
//************************* FVolumeTextureBulkData ****************************
//*****************************************************************************

void FVolumeTextureBulkData::Discard()
{
	for (int32 MipIndex = 0; MipIndex < MAX_TEXTURE_MIP_COUNT; ++MipIndex)
	{
		if (MipData[MipIndex])
		{
			FMemory::Free(MipData[MipIndex]);
			MipData[MipIndex] = nullptr;
		}
		MipSize[MipIndex] = 0;
	}
}

void FVolumeTextureBulkData::MergeMips(int32 NumMips)
{
	check(NumMips < MAX_TEXTURE_MIP_COUNT);

	uint64 MergedSize = 0;
	for (int32 MipIndex = FirstMipIdx; MipIndex < NumMips; ++MipIndex)
	{
		MergedSize += MipSize[MipIndex];
	}

	// Don't do anything if there is nothing to merge
	if (MergedSize > MipSize[FirstMipIdx])
	{
		uint8* MergedAlloc = (uint8*)FMemory::Malloc(MergedSize);
		uint8* CurrPos = MergedAlloc;
		for (int32 MipIndex = FirstMipIdx; MipIndex < NumMips; ++MipIndex)
		{
			if (MipData[MipIndex])
			{
				FMemory::Memcpy(CurrPos, MipData[MipIndex], MipSize[MipIndex]);
			}
			CurrPos += MipSize[MipIndex];
		}

		Discard();

		MipData[FirstMipIdx] = MergedAlloc;
		MipSize[FirstMipIdx] = MergedSize;
	}
}

//*****************************************************************************
//*************************** FTexture3DResource ******************************
//*****************************************************************************

FTexture3DResource::FTexture3DResource(UVolumeTexture* InOwner, const FStreamableRenderResourceState& InState)
: FStreamableTextureResource(InOwner, InOwner->PlatformData, InState, false)
, InitialData(InState.RequestedFirstLODIdx())
{
	const int32 FirstLODIdx = InState.RequestedFirstLODIdx();
	if (PlatformData && const_cast<FTexturePlatformData*>(PlatformData)->TryLoadMips(FirstLODIdx + InState.AssetLODBias, InitialData.GetMipData() + FirstLODIdx, InOwner))
	{
		// Compute the size of each mips so that they can be merged into a single allocation.
		if (GUseTexture3DBulkDataRHI)
		{
			for (int32 MipIndex = FirstLODIdx; MipIndex < InState.MaxNumLODs; ++MipIndex)
			{
				const FTexture2DMipMap& MipMap = PlatformData->Mips[MipIndex];
				
				// The bulk data can be bigger because of memory alignment constraints on each slice and mips.
				InitialData.GetMipSize()[MipIndex] = FMath::Max<int32>(
					MipMap.BulkData.GetBulkDataSize(), 
					CalcTextureMipMapSize3D(SizeX, SizeY, SizeZ, (EPixelFormat)PixelFormat, MipIndex)
				);
			}
		}

	}
}

void FTexture3DResource::CreateTexture()
{
	TArrayView<const FTexture2DMipMap*> MipsView = GetPlatformMipsView();
	const int32 FirstMipIdx = InitialData.GetFirstMipIdx(); // == State.RequestedFirstLODIdx()
	FTexture3DRHIRef Texture3DRHI;

	// Create the RHI texture.
	{
		FRHIResourceCreateInfo CreateInfo;
		if (GUseTexture3DBulkDataRHI)
		{
			InitialData.MergeMips(State.MaxNumLODs);
			CreateInfo.BulkData = &InitialData;
		}

		const FTexture2DMipMap& FirstMip = *MipsView[FirstMipIdx];
		CreateInfo.ExtData = PlatformData->GetExtData();
		Texture3DRHI = RHICreateTexture3D(FirstMip.SizeX, FirstMip.SizeY, FirstMip.SizeZ, PixelFormat, State.NumRequestedLODs, CreationFlags, CreateInfo);
		TextureRHI = Texture3DRHI; 
	}

	if (!GUseTexture3DBulkDataRHI) 
	{
		const int32 BlockSizeX = GPixelFormats[PixelFormat].BlockSizeX;
		const int32 BlockSizeY = GPixelFormats[PixelFormat].BlockSizeY;
		const int32 BlockBytes = GPixelFormats[PixelFormat].BlockBytes;
		ensure(GPixelFormats[PixelFormat].BlockSizeZ == 1);

		for (int32 RHIMipIdx = 0; RHIMipIdx < State.NumRequestedLODs; ++RHIMipIdx)
		{
			const int32 ResourceMipIdx = RHIMipIdx + FirstMipIdx;
			const FTexture2DMipMap& Mip = *MipsView[ResourceMipIdx];
			const uint8* MipData = (const uint8*)InitialData.GetMipData()[ResourceMipIdx];
			if (MipData)
			{
				FUpdateTextureRegion3D UpdateRegion(0, 0, 0, 0, 0, 0, Mip.SizeX, Mip.SizeY, Mip.SizeZ);

				// RHIUpdateTexture3D crashes on some platforms at engine initialization time.
				// The default volume texture end up being loaded at that point, which is a problem.
				// We check if this is really the rendering thread to find out if the engine is initializing.
				const uint32 NumBlockX = (uint32)FMath::DivideAndRoundUp<int32>(Mip.SizeX, BlockSizeX);
				const uint32 NumBlockY = (uint32)FMath::DivideAndRoundUp<int32>(Mip.SizeY, BlockSizeY);
				RHIUpdateTexture3D(Texture3DRHI, RHIMipIdx, UpdateRegion, NumBlockX * BlockBytes, NumBlockX * NumBlockY * BlockBytes, MipData);
			}
		}
		InitialData.Discard();
	}
}

void FTexture3DResource::CreatePartiallyResidentTexture()
{
	unimplemented();
	TextureRHI.SafeRelease();
}

#if STATS
void FTexture3DResource::CalcRequestedMipsSize()
{
	if (PlatformData && State.NumRequestedLODs > 0)
	{
		uint32 MipExtentX = 0;
		uint32 MipExtentY = 0;
		uint32 MipExtentZ = 0;
		CalcMipMapExtent3D(SizeX, SizeY, SizeZ, PixelFormat, State.RequestedFirstLODIdx(), MipExtentX, MipExtentY, MipExtentZ);

		uint32 TextureAlign = 0;
		TextureSize = (uint32)RHICalcTexture3DPlatformSize(MipExtentX, MipExtentY, MipExtentZ, PixelFormat, State.NumRequestedLODs, CreationFlags, FRHIResourceCreateInfo(PlatformData->GetExtData()), TextureAlign);
	}
	else
	{
		TextureSize = 0;
	}
}
#endif
