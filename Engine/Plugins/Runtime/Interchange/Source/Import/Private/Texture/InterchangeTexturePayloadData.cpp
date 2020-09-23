// Copyright Epic Games, Inc. All Rights Reserved.

#include "Texture/InterchangeTexturePayloadData.h"

#include "CoreMinimal.h"
#include "Engine/Texture.h"
#include "HAL/IConsoleManager.h"
#include "Misc/MessageDialog.h"
#include "RHI.h"

bool UE::Interchange::FImportImageHelper::IsImportResolutionValid(int32 Width, int32 Height, bool bAllowNonPowerOfTwo)
{
	static const auto CVarVirtualTexturesEnabled = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.VirtualTextures")); check(CVarVirtualTexturesEnabled);

	// In theory this value could be much higher, but various UE4 image code currently uses 32bit size/offset values
	const int32 MaximumSupportedVirtualTextureResolution = 16 * 1024;

	// Calculate the maximum supported resolution utilizing the global max texture mip count
	// (Note, have to subtract 1 because 1x1 is a valid mip-size; this means a GMaxTextureMipCount of 4 means a max resolution of 8x8, not 2^4 = 16x16)
	const int32 MaximumSupportedResolution = CVarVirtualTexturesEnabled->GetValueOnAnyThread() ? MaximumSupportedVirtualTextureResolution : (1 << (GMaxTextureMipCount - 1));

	bool bValid = true;

	// Check if the texture is above the supported resolution and prompt the user if they wish to continue if it is
	if (Width > MaximumSupportedResolution || Height > MaximumSupportedResolution)
	{
		if (EAppReturnType::Yes != FMessageDialog::Open(EAppMsgType::YesNo, FText::Format(
			NSLOCTEXT("Interchange", "Warning_LargeTextureImport", "Attempting to import {0} x {1} texture, proceed?\nLargest supported texture size: {2} x {3}"),
			FText::AsNumber(Width), FText::AsNumber(Height), FText::AsNumber(MaximumSupportedResolution), FText::AsNumber(MaximumSupportedResolution))))
		{
			bValid = false;
		}

		if (bValid && (Width * Height) > FMath::Square(MaximumSupportedVirtualTextureResolution))
		{
			//Warn->Log(ELogVerbosity::Error, *NSLOCTEXT("UnrealEd", "Warning_TextureSizeTooLarge", "Texture is too large to import").ToString());
			bValid = false;
		}
	}

	const bool bIsPowerOfTwo = FMath::IsPowerOfTwo(Width) && FMath::IsPowerOfTwo(Height);
	// Check if the texture dimensions are powers of two
	if (!bAllowNonPowerOfTwo && !bIsPowerOfTwo)
	{
		//Warn->Log(ELogVerbosity::Error, *NSLOCTEXT("UnrealEd", "Warning_TextureNotAPowerOfTwo", "Cannot import texture with non-power of two dimensions").ToString());
		bValid = false;
	}

	return bValid;
}

void UE::Interchange::FImportImage::Init2DWithParams(int32 InSizeX, int32 InSizeY, ETextureSourceFormat InFormat, bool InSRGB)
{
	SizeX = InSizeX;
	SizeY = InSizeY;
	NumMips = 1;
	Format = InFormat;
	SRGB = InSRGB;
}

void UE::Interchange::FImportImage::Init2DWithOneMip(int32 InSizeX, int32 InSizeY, ETextureSourceFormat InFormat, const void* InData)
{
#if WITH_EDITOR
	SizeX = InSizeX;
	SizeY = InSizeY;
	NumMips = 1;
	Format = InFormat;
	RawData.AddUninitialized((int64)SizeX * SizeY * FTextureSource::GetBytesPerPixel(Format));
	if (InData)
	{
		FMemory::Memcpy(RawData.GetData(), InData, RawData.Num());
	}
#endif
}

void UE::Interchange::FImportImage::Init2DWithMips(int32 InSizeX, int32 InSizeY, int32 InNumMips, ETextureSourceFormat InFormat, const void* InData)
{
	SizeX = InSizeX;
	SizeY = InSizeY;
	NumMips = InNumMips;
	Format = InFormat;

	int64 TotalSize = 0;
	for (int32 MipIndex = 0; MipIndex < InNumMips; ++MipIndex)
	{
		TotalSize += GetMipSize(MipIndex);
	}
	RawData.AddUninitialized(TotalSize);

	if (InData)
	{
		FMemory::Memcpy(RawData.GetData(), InData, RawData.Num());
	}
}

int64 UE::Interchange::FImportImage::GetMipSize(int32 InMipIndex) const
{
#if WITH_EDITOR
	check(InMipIndex >= 0);
	check(InMipIndex < NumMips);
	const int32 MipSizeX = FMath::Max(SizeX >> InMipIndex, 1);
	const int32 MipSizeY = FMath::Max(SizeY >> InMipIndex, 1);
	return (int64)MipSizeX * MipSizeY * FTextureSource::GetBytesPerPixel(Format);
#else
	return 0;
#endif
}

void* UE::Interchange::FImportImage::GetMipData(int32 InMipIndex)
{
	int64 Offset = 0;
	for (int32 MipIndex = 0; MipIndex < InMipIndex; ++MipIndex)
	{
		Offset += GetMipSize(MipIndex);
	}
	return &RawData[Offset];
}
