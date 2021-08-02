// Copyright Epic Games, Inc. All Rights Reserved.

#include "Texture/InterchangeSlicedTexturePayloadData.h"

#include "CoreMinimal.h"


void UE::Interchange::FImportSlicedImage::Init(int32 InSizeX, int32 InSizeY, int32 InNumSlice, int32 InNumMips, ETextureSourceFormat InFormat, bool InSRGB)
{
	NumSlice = InNumSlice;

	FImportImage::Init2DWithParams(InSizeX, InSizeY, InNumMips, InFormat, InSRGB);
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

	check(RawData.GetSize() > uint64(Offset));

	return static_cast<const uint8*>(RawData.GetData()) + Offset;
}

uint8* UE::Interchange::FImportSlicedImage::GetMipData(int32 InMipIndex, int32 InSliceIndex)
{
	const uint8* ConstBufferPtr = static_cast<const FImportSlicedImage*>(this)->GetMipData(InMipIndex, InSliceIndex);
	return const_cast<uint8*>(ConstBufferPtr);
}

int64 UE::Interchange::FImportSlicedImage::ComputeBufferSize() const
{
	return FImportImage::ComputeBufferSize() * NumSlice;
}

bool UE::Interchange::FImportSlicedImage::IsValid() const
{
	return NumSlice > 0 && FImportImage::IsValid();
}

