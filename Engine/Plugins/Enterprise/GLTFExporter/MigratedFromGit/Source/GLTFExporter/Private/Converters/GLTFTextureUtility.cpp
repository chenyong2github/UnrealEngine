// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFTextureUtility.h"
#include "Engine/Texture2DArray.h"

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

TextureAddress FGLTFTextureUtility::GetAddressX(const UTexture* Texture)
{
	if (const UTexture2D* Texture2D = Cast<UTexture2D>(Texture))
	{
		return Texture2D->AddressX;
	}

	if (const UTexture2DArray* Texture2DArray = Cast<UTexture2DArray>(Texture))
	{
		return Texture2DArray->AddressX;
	}

	if (const UTextureRenderTarget2D* RenderTarget2D = Cast<UTextureRenderTarget2D>(Texture))
	{
		return RenderTarget2D->AddressX;
	}

	// TODO: add support for UMediaTexture?

	return TextureAddress::TA_MAX;
}

TextureAddress FGLTFTextureUtility::GetAddressY(const UTexture* Texture)
{
	if (const UTexture2D* Texture2D = Cast<UTexture2D>(Texture))
	{
		return Texture2D->AddressY;
	}

	if (const UTexture2DArray* Texture2DArray = Cast<UTexture2DArray>(Texture))
	{
		return Texture2DArray->AddressY;
	}

	if (const UTextureRenderTarget2D* RenderTarget2D = Cast<UTextureRenderTarget2D>(Texture))
	{
		return RenderTarget2D->AddressY;
	}

	// TODO: add support for UMediaTexture?

	return TextureAddress::TA_MAX;
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
