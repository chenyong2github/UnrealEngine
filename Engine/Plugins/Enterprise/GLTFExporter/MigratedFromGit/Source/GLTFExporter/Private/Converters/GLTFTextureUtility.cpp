// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFTextureUtility.h"

bool FGLTFTextureUtility::IsCubemap(const UTexture* Texture)
{
	return Texture->IsA<UTextureCube>() || Texture->IsA<UTextureRenderTargetCube>();
}

TextureFilter FGLTFTextureUtility::GetDefaultFilter(TextureGroup LODGroup)
{
	// TODO: should this be the running platform?
	const ITargetPlatform* RunningPlatform = GetTargetPlatformManagerRef().GetRunningTargetPlatform();
	const UTextureLODSettings& TextureLODSettings = RunningPlatform->GetTextureLODSettings();
	const FTextureLODGroup& TextureLODGroup = TextureLODSettings.GetTextureLODGroup(LODGroup);

	switch (TextureLODGroup.Filter)
	{
		case ETextureSamplerFilter::Point:             return TF_Nearest;
		case ETextureSamplerFilter::Bilinear:          return TF_Bilinear;
		case ETextureSamplerFilter::Trilinear:         return TF_Trilinear;
		case ETextureSamplerFilter::AnisotropicPoint:  return TF_Nearest;
		case ETextureSamplerFilter::AnisotropicLinear: return TF_Trilinear;
		default:                                       return TF_Default; // fallback
	}
}

TTuple<TextureAddress, TextureAddress> FGLTFTextureUtility::GetAddressXY(const UTexture* Texture)
{
	TextureAddress AddressX;
	TextureAddress AddressY;

	if (const UTexture2D* Texture2D = Cast<UTexture2D>(Texture))
	{
		AddressX = Texture2D->AddressX;
		AddressY = Texture2D->AddressX;
	}
	else if (const UTextureRenderTarget2D* RenderTarget2D = Cast<UTextureRenderTarget2D>(Texture))
	{
		AddressX = RenderTarget2D->AddressX;
		AddressY = RenderTarget2D->AddressX;
	}
	else
	{
		AddressX = TA_MAX;
		AddressY = TA_MAX;
	}

	return MakeTuple(AddressX, AddressY);
}

UTexture2D* FGLTFTextureUtility::CreateTransientTexture(const void* RawData, int64 ByteLength, const FIntPoint& Size, EPixelFormat Format, bool bUseSRGB)
{
	check(CalculateImageBytes(Size.X, Size.Y, 0, Format) == ByteLength);

	UTexture2D* Texture = UTexture2D::CreateTransient(Size.X, Size.Y, Format);

	void* MipData = Texture->PlatformData->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(MipData, RawData, ByteLength);
	Texture->PlatformData->Mips[0].BulkData.Unlock();

	Texture->SRGB = bUseSRGB ? 1 : 0;
	Texture->CompressionNone = true;
	Texture->MipGenSettings = TMGS_NoMipmaps;

	Texture->UpdateResource();
	return Texture;
}
