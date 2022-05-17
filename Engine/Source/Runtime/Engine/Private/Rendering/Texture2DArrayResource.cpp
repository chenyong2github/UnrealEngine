// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	Texture2DArrayResource.cpp: Implementation of FTexture2DArrayResource used  by streamable UTexture2DArray.
=============================================================================*/

#include "Rendering/Texture2DArrayResource.h"
#include "Engine/Texture2DArray.h"
#include "DeviceProfiles/DeviceProfile.h"
#include "DeviceProfiles/DeviceProfileManager.h"
#include "RenderUtils.h"

//*****************************************************************************
//************************* FTexture2DArrayResource ***************************
//*****************************************************************************

FTexture2DArrayResource::FTexture2DArrayResource(UTexture2DArray* InOwner, const FStreamableRenderResourceState& InState) 
: FStreamableTextureResource(InOwner, InOwner->PlatformData, InState, false)
{
	AddressU = InOwner->AddressX == TA_Wrap ? AM_Wrap : (InOwner->AddressX == TA_Clamp ? AM_Clamp : AM_Mirror);
	AddressV = InOwner->AddressY == TA_Wrap ? AM_Wrap : (InOwner->AddressY == TA_Clamp ? AM_Clamp : AM_Mirror);
	AddressW = InOwner->AddressZ == TA_Wrap ? AM_Wrap : (InOwner->AddressZ == TA_Clamp ? AM_Clamp : AM_Mirror);
	
	if (InOwner->GetMipData(State.RequestedFirstLODIdx(), MipData) == false)
	{
		// This is fatal as we will crash trying to upload the data below, this way we crash at the cause.
		UE_LOG(LogTexture, Fatal, TEXT("Corrupt texture [%s]! Unable to load mips (bulk data missing)"), *TextureName.ToString());
		return;
	}

	// Resize each structure correctly per slice, since we access it per mip after.
	SliceMipDataViews.AddDefaulted(SizeZ);
	for (uint32 SliceIdx = 0; SliceIdx < SizeZ; ++SliceIdx)
	{
		SliceMipDataViews[SliceIdx].AddDefaulted(State.MaxNumLODs);
	}

	const int32 RequestedFirstLODIdx = State.RequestedFirstLODIdx();
	for (int32 RHIMipIdx = 0; RHIMipIdx < State.NumRequestedLODs; ++RHIMipIdx)
	{
		const int32 MipIdx = RHIMipIdx + RequestedFirstLODIdx;
		const uint64 SliceMipDataSize = MipData[RHIMipIdx].GetSize() / SizeZ;
		uint8* SrcData = (uint8*)MipData[RHIMipIdx].GetData();

		for (uint32 SliceIdx = 0; SliceIdx < SizeZ; ++SliceIdx)
		{
			TArrayView<uint8>& SliceMipDataView = SliceMipDataViews[SliceIdx][MipIdx];
			SliceMipDataView = TArrayView<uint8>(SrcData + SliceIdx * SliceMipDataSize, SliceMipDataSize);
		}
	}
}

void FTexture2DArrayResource::CreateTexture()
{
	const int32 RequestedFirstLODIdx = State.RequestedFirstLODIdx();
	TArrayView<const FTexture2DMipMap*> MipsView = GetPlatformMipsView();
	const FTexture2DMipMap& FirstMip = *MipsView[RequestedFirstLODIdx];

	const FRHITextureCreateDesc Desc =
		FRHITextureCreateDesc::Create2DArray(TEXT("FTexture2DArrayResource"), FirstMip.SizeX, FirstMip.SizeY, FirstMip.SizeZ, PixelFormat)
		.SetNumMips(State.NumRequestedLODs)
		.SetFlags(CreationFlags)
		.SetExtData(PlatformData->GetExtData());

	TextureRHI = RHICreateTexture(Desc);

	// Read the initial cached mip levels into the RHI texture.
	const int32 NumLoadableMips = State.NumRequestedLODs - FMath::Max(1, PlatformData->GetNumMipsInTail()) + 1;
	for (int32 RHIMipIdx = 0; RHIMipIdx < NumLoadableMips; ++RHIMipIdx)
	{
		const int32 MipIdx = RHIMipIdx + RequestedFirstLODIdx;
		for (uint32 SliceIdx = 0; SliceIdx < SizeZ; ++SliceIdx)
		{
			uint32 DestStride = 0;
			void* DestData = RHILockTexture2DArray(TextureRHI, SliceIdx, RHIMipIdx, RLM_WriteOnly, DestStride, false);
			if (DestData)
			{
				GetData(SliceIdx, MipIdx, DestData, DestStride);
			}
			RHIUnlockTexture2DArray(TextureRHI, SliceIdx, RHIMipIdx, false);
		}
	}
		
	SliceMipDataViews.Empty();
	MipData.Empty();
}

void FTexture2DArrayResource::CreatePartiallyResidentTexture()
{
	unimplemented();
	TextureRHI.SafeRelease();
}

uint64 FTexture2DArrayResource::GetPlatformMipsSize(uint32 NumMips) const
{
	if (PlatformData && NumMips > 0)
	{
		const FIntPoint MipExtents = CalcMipMapExtent(SizeX, SizeY, PixelFormat, State.LODCountToFirstLODIdx(NumMips));
		uint32 TextureAlign = 0;
		return RHICalcTexture2DArrayPlatformSize(MipExtents.X, MipExtents.Y, SizeZ, PixelFormat, NumMips, 1, CreationFlags, FRHIResourceCreateInfo(PlatformData->GetExtData()), TextureAlign);
	}
	else
	{
		return 0;
	}
}

void FTexture2DArrayResource::GetData(uint32 SliceIndex, uint32 MipIndex, void* Dest, uint32 DestPitch)
{
	const TArrayView<uint8>& SliceMipDataView = SliceMipDataViews[SliceIndex][MipIndex];

	// for platforms that returned 0 pitch from Lock, we need to just use the bulk data directly, never do 
	// runtime block size checking, conversion, or the like
	if (DestPitch == 0)
	{
		FMemory::Memcpy(Dest, SliceMipDataView.GetData(), SliceMipDataView.Num());
	}
	else
	{
		const int32 BlockSizeX = GPixelFormats[PixelFormat].BlockSizeX;
		const int32 BlockSizeY = GPixelFormats[PixelFormat].BlockSizeY;
		const int32 BlockBytes = GPixelFormats[PixelFormat].BlockBytes;

		const uint32 MipSizeX = FMath::Max<int32>(SizeX >> MipIndex, 1);
		const uint32 MipSizeY = FMath::Max<int32>(SizeY >> MipIndex, 1);

		uint32 NumColumns = FMath::DivideAndRoundUp<int32>(MipSizeX, BlockSizeX);
		uint32 NumRows = FMath::DivideAndRoundUp<int32>(MipSizeY, BlockSizeY);

		if (PixelFormat == PF_PVRTC2 || PixelFormat == PF_PVRTC4)
		{
			// PVRTC has minimum 2 blocks width and height
			NumColumns = FMath::Max<uint32>(NumColumns, 2);
			NumRows = FMath::Max<uint32>(NumRows, 2);
		}
		const uint32 SrcStride = NumColumns * BlockBytes;

		CopyTextureData2D(SliceMipDataView.GetData(), Dest, MipSizeY, PixelFormat, SrcStride, DestPitch);
	}
}
