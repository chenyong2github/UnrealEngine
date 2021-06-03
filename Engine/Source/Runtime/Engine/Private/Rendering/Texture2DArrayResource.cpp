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

	const int32 MaxLoadableMipIndex = State.MaxNumLODs - FMath::Max(1, PlatformData->GetNumMipsInTail()) + 1;

	TArray<uint64> MipOffsets;
	MipOffsets.AddZeroed(State.MaxNumLODs);

	uint64 InitialMipDataSize = 0;
	TArrayView<const FTexture2DMipMap*> MipsView = GetPlatformMipsView();
	for (int32 MipIdx = State.RequestedFirstLODIdx(); MipIdx < MaxLoadableMipIndex; ++MipIdx)
	{
		const FTexture2DMipMap& Mip = *MipsView[MipIdx];
		MipOffsets[MipIdx] = InitialMipDataSize;
		InitialMipDataSize += Mip.BulkData.GetBulkDataSize();
	}
	InitialMipData.Reset(new uint8[InitialMipDataSize]);

	// Resize each structure correctly per slice, since we access it per mip after.
	SliceMipDataViews.AddDefaulted(SizeZ);
	for (uint32 SliceIdx = 0; SliceIdx < SizeZ; ++SliceIdx)
	{
		SliceMipDataViews[SliceIdx].AddDefaulted(MaxLoadableMipIndex);
	}

	for (int32 MipIdx = State.RequestedFirstLODIdx(); MipIdx < MaxLoadableMipIndex; ++MipIdx)
	{
		FTexture2DMipMap& Mip = const_cast<FTexture2DMipMap&>(*MipsView[MipIdx]);
		if (Mip.BulkData.GetBulkDataSize() > 0)
		{
			const uint64 SliceMipDataSize = Mip.BulkData.GetBulkDataSize() / SizeZ;
			const uint8* SrcData = (const uint8*)Mip.BulkData.Lock(LOCK_READ_ONLY);

			for (uint32 SliceIdx = 0; SliceIdx < SizeZ; ++SliceIdx)
			{
				TArrayView<uint8>& SliceMipDataView = SliceMipDataViews[SliceIdx][MipIdx];
				SliceMipDataView = TArrayView<uint8>(InitialMipData.Get() + MipOffsets[MipIdx] + SliceIdx * SliceMipDataSize, SliceMipDataSize);
				FMemory::Memcpy(SliceMipDataView.GetData(), SrcData + SliceIdx * SliceMipDataSize, SliceMipDataSize);
			}

			Mip.BulkData.Unlock();
		}
		else
		{
			UE_LOG(LogTexture, Error, TEXT("Corrupt texture [%s]! Missing bulk data for MipIndex=%d"), *TextureName.ToString(), MipIdx);
		}
	}
}

void FTexture2DArrayResource::CreateTexture()
{
	const int32 RequestedFirstLODIdx = State.RequestedFirstLODIdx();
	TArrayView<const FTexture2DMipMap*> MipsView = GetPlatformMipsView();
	const FTexture2DMipMap& FirstMip = *MipsView[RequestedFirstLODIdx];

	FRHIResourceCreateInfo CreateInfo;
	TRefCountPtr<FRHITexture2DArray> TextureArray = RHICreateTexture2DArray(FirstMip.SizeX, FirstMip.SizeY, FirstMip.SizeZ, PixelFormat, State.NumRequestedLODs, 1, CreationFlags, CreateInfo);
	TextureRHI = TextureArray;

	// Read the initial cached mip levels into the RHI texture.
	const int32 NumLoadableMips = State.NumRequestedLODs - FMath::Max(1, PlatformData->GetNumMipsInTail()) + 1;
	for (int32 RHIMipIdx = 0; RHIMipIdx < NumLoadableMips; ++RHIMipIdx)
	{
		const int32 MipIdx = RHIMipIdx + RequestedFirstLODIdx;
		for (uint32 SliceIdx = 0; SliceIdx < SizeZ; ++SliceIdx)
		{
			uint32 DestStride = 0;
			void* DestData = RHILockTexture2DArray(TextureArray, SliceIdx, RHIMipIdx, RLM_WriteOnly, DestStride, false);
			if (DestData)
			{
				GetData(SliceIdx, MipIdx, DestData, DestStride);
			}
			RHIUnlockTexture2DArray(TextureArray, SliceIdx, RHIMipIdx, false);
		}
	}
		
	SliceMipDataViews.Empty();
	InitialMipData.Reset();
}

void FTexture2DArrayResource::CreatePartiallyResidentTexture()
{
	unimplemented();
	TextureRHI.SafeRelease();
}

#if STATS
void FTexture2DArrayResource::CalcRequestedMipsSize()
{
	if (PlatformData && State.NumRequestedLODs > 0)
	{
		const FIntPoint MipExtents = CalcMipMapExtent(SizeX, SizeY, PixelFormat, State.RequestedFirstLODIdx());
		uint32 TextureAlign = 0;
		TextureSize = SizeZ * RHICalcTexture2DPlatformSize(MipExtents.X, MipExtents.Y, PixelFormat, State.NumRequestedLODs, 1, CreationFlags, FRHIResourceCreateInfo(PlatformData->GetExtData()), TextureAlign);
	}
	else
	{
		TextureSize = 0;
	}
}
#endif

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
