// Copyright Epic Games, Inc. All Rights Reserved.

#include "Texture/InterchangeSlicedTexturePayloadData.h"

#include "CoreMinimal.h"
#include "Engine/Texture.h"


void UE::Interchange::FImportSlicedImage::Init(int32 InSizeX, int32 InSizeY, int32 InNumSlice, int32 InNumMips, ETextureSourceFormat InFormat, bool InSRGB)
{
	SizeX = InSizeX;
	SizeY = InSizeY;
	NumSlice = InNumSlice;
	NumMips = InNumMips;
	Format = InFormat;
	bSRGB = InSRGB;
}

int64 UE::Interchange::FImportSlicedImage::GetMipSize(int32 InMipIndex) const
{
	check(InMipIndex >= 0);
	check(InMipIndex < NumMips);
	const int32 MipSizeX = FMath::Max(SizeX >> InMipIndex, 1);
	const int32 MipSizeY = FMath::Max(SizeY >> InMipIndex, 1);
	return (int64)MipSizeX * MipSizeY * FTextureSource::GetBytesPerPixel(Format);

}

const uint8* UE::Interchange::FImportSlicedImage::GetMipData(int32 InMipIndex, int32 InSliceIndex) const
{
	int64 Offset = 0;
	int32 MipIndex;
	for (MipIndex = 0; MipIndex < InMipIndex; ++MipIndex)
	{
		Offset += GetMipSize(MipIndex) * NumSlice;
	}

	if (InSliceIndex != INDEX_NONE)
	{ 
		Offset += GetMipSize(MipIndex) * InSliceIndex;
	}

	return &RawData[Offset];
}

uint8* UE::Interchange::FImportSlicedImage::GetMipData(int32 InMipIndex, int32 InSliceIndex)
{
	const uint8* ConstBufferPtr = static_cast<const FImportSlicedImage*>(this)->GetMipData(InMipIndex, InSliceIndex);
	return const_cast<uint8*>(ConstBufferPtr);
}
