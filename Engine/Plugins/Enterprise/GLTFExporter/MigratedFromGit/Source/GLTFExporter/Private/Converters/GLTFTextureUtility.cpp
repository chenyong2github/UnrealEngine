// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFTextureUtility.h"
#include "IImageWrapper.h"

bool FGLTFTextureUtility::IsHDRFormat(EPixelFormat Format)
{
	return CalculateImageBytes(1, 1, 0, Format) > 4;
}

bool FGLTFTextureUtility::CanPNGCompressFormat(ETextureSourceFormat InFormat, ERGBFormat& OutFormat, uint32& OutBitDepth)
{
	switch (InFormat)
	{
		case TSF_BGRA8:   OutFormat = ERGBFormat::BGRA; OutBitDepth = 8;  return true;
		case TSF_RGBA8:   OutFormat = ERGBFormat::RGBA; OutBitDepth = 8;  return true;
		case TSF_RGBA16:  OutFormat = ERGBFormat::RGBA; OutBitDepth = 16; return true;
		case TSF_G8:      OutFormat = ERGBFormat::Gray; OutBitDepth = 8;  return true;
		case TSF_G16:     OutFormat = ERGBFormat::Gray; OutBitDepth = 16; return true;
		default:
            return false;
	}
}

bool FGLTFTextureUtility::CanPNGCompressFormat(EPixelFormat InFormat, ERGBFormat& OutFormat, uint32& OutBitDepth)
{
	switch (InFormat)
	{
		case PF_B8G8R8A8:           OutFormat = ERGBFormat::BGRA; OutBitDepth = 8;  return true;
		case PF_R8G8B8A8:           OutFormat = ERGBFormat::RGBA; OutBitDepth = 8;  return true;
		case PF_R16G16B16A16_UNORM: OutFormat = ERGBFormat::RGBA; OutBitDepth = 16; return true;
		case PF_L8:                 OutFormat = ERGBFormat::Gray; OutBitDepth = 8;  return true;
		case PF_G8:                 OutFormat = ERGBFormat::Gray; OutBitDepth = 8;  return true;
		case PF_G16:                OutFormat = ERGBFormat::Gray; OutBitDepth = 16; return true;
		default:
            return false;
	}
}

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

UTextureRenderTarget2D* FGLTFTextureUtility::CreateRenderTarget(const FIntPoint& Size, EPixelFormat Format, bool bInForceLinearGamma)
{
	UTextureRenderTarget2D* RenderTarget = NewObject<UTextureRenderTarget2D>();
	RenderTarget->InitCustomFormat(Size.X, Size.Y, Format, bInForceLinearGamma);
	return RenderTarget;
}

bool FGLTFTextureUtility::DrawTexture(UTextureRenderTarget2D* OutTarget, const UTexture2D* InSource)
{
	FRenderTarget* RenderTarget = OutTarget->GameThread_GetRenderTargetResource();
	if (RenderTarget == nullptr)
	{
		return false;
	}

	FCanvas Canvas(RenderTarget, nullptr, 0.0f, 0.0f, 0.0f, GMaxRHIFeatureLevel);
	FCanvasTileItem TileItem(FVector2D::ZeroVector, InSource->Resource, FLinearColor::White);

	TileItem.Draw(&Canvas);

	Canvas.Flush_GameThread();
	FlushRenderingCommands();
	Canvas.SetRenderTarget_GameThread(nullptr);
	FlushRenderingCommands();

	return true;
}
