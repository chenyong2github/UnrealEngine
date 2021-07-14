// Copyright Epic Games, Inc. All Rights Reserved.

#include "BakeMeshAttributeToolCommon.h"
#include "Engine/Classes/Engine/Texture2D.h"

FTempTextureAccess::FTempTextureAccess(UTexture2D* DisplacementMap)
	: DisplacementMap(DisplacementMap)
{
	check(DisplacementMap);
	OldCompressionSettings = DisplacementMap->CompressionSettings;
	bOldSRGB = DisplacementMap->SRGB;
#if WITH_EDITOR
	OldMipGenSettings = DisplacementMap->MipGenSettings;
#endif
	DisplacementMap->CompressionSettings = TextureCompressionSettings::TC_VectorDisplacementmap;
	DisplacementMap->SRGB = false;
#if WITH_EDITOR
	DisplacementMap->MipGenSettings = TextureMipGenSettings::TMGS_NoMipmaps;
#endif
	DisplacementMap->UpdateResource();

	FormattedImageData = reinterpret_cast<const FColor*>(DisplacementMap->PlatformData->Mips[0].BulkData.LockReadOnly());
}

FTempTextureAccess::~FTempTextureAccess()
{
	DisplacementMap->PlatformData->Mips[0].BulkData.Unlock();
	DisplacementMap->CompressionSettings = OldCompressionSettings;
	DisplacementMap->SRGB = bOldSRGB;
#if WITH_EDITOR
	DisplacementMap->MipGenSettings = OldMipGenSettings;
#endif
	DisplacementMap->UpdateResource();
}

bool FTempTextureAccess::HasData() const
{
	return FormattedImageData != nullptr;
}
	
const FColor* FTempTextureAccess::GetData() const
{
	return FormattedImageData;
}

FImageDimensions FTempTextureAccess::GetDimensions() const
{
	const int32 Width = DisplacementMap->PlatformData->Mips[0].SizeX;
	const int32 Height = DisplacementMap->PlatformData->Mips[0].SizeY;
	return FImageDimensions(Width, Height);
}

bool FTempTextureAccess::CopyTo(TImageBuilder<FVector4f>& DestImage) const
{
	if (!HasData()) return false;

	const FImageDimensions TextureDimensions = GetDimensions();
	if (ensure(DestImage.GetDimensions() == TextureDimensions) == false)
	{
		return false;
	}

	const int64 Num = TextureDimensions.Num();
	for (int32 i = 0; i < Num; ++i)
	{
		FColor ByteColor = FormattedImageData[i];
		FLinearColor FloatColor(ByteColor);
		DestImage.SetPixel(i, FVector4f(FloatColor));
	}
	return true;
}

