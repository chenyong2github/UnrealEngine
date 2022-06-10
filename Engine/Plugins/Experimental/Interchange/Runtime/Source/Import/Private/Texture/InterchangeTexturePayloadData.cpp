// Copyright Epic Games, Inc. All Rights Reserved.

#include "Texture/InterchangeTexturePayloadData.h"

#include "CoreMinimal.h"
#include "Engine/Texture.h"
#include "HAL/IConsoleManager.h"
#include "Misc/MessageDialog.h"
#include "RHI.h"

bool UE::Interchange::FImportImageHelper::IsImportResolutionValid(int32 Width, int32 Height, bool bAllowNonPowerOfTwo, FText* OutErrorMessage)
{
	static const auto CVarVirtualTexturesEnabled = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.VirtualTextures")); check(CVarVirtualTexturesEnabled);

	// In theory this value could be much higher, but various UE image code currently uses 32bit size/offset values
	const int32 MaximumSupportedVirtualTextureResolution = 16 * 1024;

	// Calculate the maximum supported resolution utilizing the global max texture mip count
	// (Note, have to subtract 1 because 1x1 is a valid mip-size; this means a GMaxTextureMipCount of 4 means a max resolution of 8x8, not 2^4 = 16x16)
	const int32 MaximumSupportedResolution = CVarVirtualTexturesEnabled->GetValueOnAnyThread() ? MaximumSupportedVirtualTextureResolution : (1 << (GMaxTextureMipCount - 1));

	// Check if the texture is above the supported resolution and prompt the user if they wish to continue if it is
	if (Width > MaximumSupportedResolution || Height > MaximumSupportedResolution)
	{
		if ((Width * Height) > FMath::Square(MaximumSupportedVirtualTextureResolution))
		{
			if (OutErrorMessage)
			{
				*OutErrorMessage = NSLOCTEXT("Interchange", "Warning_TextureSizeTooLarge", "Texture is too large to import");
			}
			
			return false;
		}
	}

	// Check if the texture dimensions are powers of two
	if (!bAllowNonPowerOfTwo)
	{
		const bool bIsPowerOfTwo = FMath::IsPowerOfTwo(Width) && FMath::IsPowerOfTwo(Height);
		if (!bIsPowerOfTwo)
		{
			*OutErrorMessage = NSLOCTEXT("Interchange", "Warning_TextureNotAPowerOfTwo", "Cannot import texture with non-power of two dimensions");
			return false;
		}
	}

	return true;
}

void UE::Interchange::FImportImage::Init2DWithParams(int32 InSizeX, int32 InSizeY, ETextureSourceFormat InFormat, bool bInSRGB, bool bShouldAllocateRawData)
{
	Init2DWithParams(InSizeX, InSizeY, 1, InFormat, bInSRGB, bShouldAllocateRawData);
}

void UE::Interchange::FImportImage::Init2DWithParams(int32 InSizeX, int32 InSizeY, int32 InNumMips, ETextureSourceFormat InFormat, bool bInSRGB, bool bShouldAllocateRawData)
{
	SizeX = InSizeX;
	SizeY = InSizeY;
	NumMips = InNumMips;
	Format = InFormat;
	bSRGB = bInSRGB;
	if (bShouldAllocateRawData)
	{
		RawData = FUniqueBuffer::Alloc(ComputeBufferSize());
	}
}

void UE::Interchange::FImportImage::Init2DWithOneMip(int32 InSizeX, int32 InSizeY, ETextureSourceFormat InFormat, const void* InData)
{
	Init2DWithParams(InSizeX, InSizeY, 1, InFormat, bSRGB);

	if (InData)
	{
		FMemory::Memcpy(RawData.GetData(), InData, RawData.GetSize());
	}
}

void UE::Interchange::FImportImage::Init2DWithMips(int32 InSizeX, int32 InSizeY, int32 InNumMips, ETextureSourceFormat InFormat, const void* InData)
{
	Init2DWithParams(InSizeX, InSizeY, 1, InFormat, bSRGB);

	if (InData)
	{
		FMemory::Memcpy(RawData.GetData(), InData, RawData.GetSize());
	}
}

int64 UE::Interchange::FImportImage::GetMipSize(int32 InMipIndex) const
{
	check(InMipIndex >= 0);
	check(InMipIndex < NumMips);
	const int32 MipSizeX = FMath::Max(SizeX >> InMipIndex, 1);
	const int32 MipSizeY = FMath::Max(SizeY >> InMipIndex, 1);
	return (int64)MipSizeX * MipSizeY * FTextureSource::GetBytesPerPixel(Format);
}

int64 UE::Interchange::FImportImage::ComputeBufferSize() const
{
	int64 TotalSize = 0;
	for (int32 MipIndex = 0; MipIndex < NumMips; ++MipIndex)
	{
		TotalSize += GetMipSize(MipIndex);
	}

	return TotalSize;
}

TArrayView64<uint8> UE::Interchange::FImportImage::GetArrayViewOfRawData()
{
	return TArrayView64<uint8>(static_cast<uint8*>(RawData.GetData()), RawData.GetSize());
}

bool UE::Interchange::FImportImage::IsValid() const
{
	bool bIsRawDataBufferValid = false;

	if (RawDataCompressionFormat == TSCF_None)
	{
		bIsRawDataBufferValid = ComputeBufferSize() == RawData.GetSize();
	}
	else
	{
		bIsRawDataBufferValid = !RawData.IsNull();
	}

	return SizeX > 0
		&& SizeY > 0
		&& NumMips > 0
		&& Format != TSF_Invalid
		&& bIsRawDataBufferValid;
}
