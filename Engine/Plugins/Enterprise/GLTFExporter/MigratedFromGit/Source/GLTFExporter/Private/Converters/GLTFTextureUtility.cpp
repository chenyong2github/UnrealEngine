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

bool FGLTFTextureUtility::HasAnyAdjustment(const UTexture* Texture)
{
	const float ErrorTolerance = KINDA_SMALL_NUMBER;

	return !FMath::IsNearlyEqual(Texture->AdjustBrightness, 1.0f, ErrorTolerance) ||
		!FMath::IsNearlyEqual(Texture->AdjustBrightnessCurve, 1.0f, ErrorTolerance) ||
		!FMath::IsNearlyEqual(Texture->AdjustSaturation, 1.0f, ErrorTolerance) ||
		!FMath::IsNearlyEqual(Texture->AdjustVibrance, 0.0f, ErrorTolerance) ||
		!FMath::IsNearlyEqual(Texture->AdjustRGBCurve, 1.0f, ErrorTolerance) ||
		!FMath::IsNearlyEqual(Texture->AdjustHue, 0.0f, ErrorTolerance) ||
		!FMath::IsNearlyEqual(Texture->AdjustMinAlpha, 0.0f, ErrorTolerance) ||
		!FMath::IsNearlyEqual(Texture->AdjustMaxAlpha, 1.0f, ErrorTolerance) ||
		Texture->bChromaKeyTexture;
}

float FGLTFTextureUtility::GetCubeFaceRotation(ECubeFace CubeFace)
{
	switch (CubeFace)
	{
		case CubeFace_PosX: return 90;
		case CubeFace_NegX: return -90;
		case CubeFace_PosY: return 180;
		case CubeFace_NegY: return 0;
		case CubeFace_PosZ: return 180;
		case CubeFace_NegZ: return 0;
		default:            return 0;
	}
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

	// TODO: do these temp textures need to be part of the root set to avoid garbage collection?
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

bool FGLTFTextureUtility::DrawTexture(UTextureRenderTarget2D* OutTarget, const UTexture2D* InSource, const FMatrix& InTransform)
{
	FRenderTarget* RenderTarget = OutTarget->GameThread_GetRenderTargetResource();
	if (RenderTarget == nullptr)
	{
		return false;
	}

	// Fully stream in the texture before drawing it.
	const_cast<UTexture2D*>(InSource)->SetForceMipLevelsToBeResident(30.0f, true);
	const_cast<UTexture2D*>(InSource)->WaitForStreaming();

	FCanvas Canvas(RenderTarget, nullptr, 0.0f, 0.0f, 0.0f, GMaxRHIFeatureLevel);
	FCanvasTileItem TileItem(FVector2D::ZeroVector, InSource->Resource, FLinearColor::White);

	Canvas.PushAbsoluteTransform(InTransform);
	TileItem.Draw(&Canvas);
	Canvas.PopTransform();

	Canvas.Flush_GameThread();
	FlushRenderingCommands();
	Canvas.SetRenderTarget_GameThread(nullptr);
	FlushRenderingCommands();

	return true;
}

bool FGLTFTextureUtility::RotateTexture(UTextureRenderTarget2D* OutTarget, const UTexture2D* InSource, float InDegrees)
{
	FMatrix Transform = FMatrix::Identity;
	if (InDegrees != 0)
	{
		const FVector Center = FVector(InSource->GetSizeX() / 2.0f, InSource->GetSizeY() / 2.0f, 0);
		Transform = FTranslationMatrix(-Center) * FRotationMatrix({ 0, InDegrees, 0 }) * FTranslationMatrix(Center);
	}

	return DrawTexture(OutTarget, InSource, Transform);
}

UTexture2D* FGLTFTextureUtility::CreateTextureFromCubeFace(const UTextureCube* TextureCube, ECubeFace CubeFace)
{
	const FIntPoint Size(TextureCube->GetSizeX(), TextureCube->GetSizeY());
	const EPixelFormat Format = TextureCube->GetPixelFormat();

	if (!LoadPlatformData(const_cast<UTextureCube*>(TextureCube)))
	{
		return nullptr;
	}

	const FByteBulkData& BulkData = TextureCube->PlatformData->Mips[0].BulkData;
	const int64 MipSize = BulkData.GetBulkDataSize() / 6;

	const void* MipDataPtr = BulkData.LockReadOnly();
	const void* FaceDataPtr =  static_cast<const uint8*>(MipDataPtr) + MipSize * CubeFace;
	UTexture2D* FaceTexture = CreateTransientTexture(FaceDataPtr, MipSize, Size, Format, TextureCube->SRGB);
	BulkData.Unlock();

	return FaceTexture;
}

UTexture2D* FGLTFTextureUtility::CreateTextureFromCubeFace(const UTextureRenderTargetCube* RenderTargetCube, ECubeFace CubeFace)
{
	UTexture2D* FaceTexture;
	const EPixelFormat Format = RenderTargetCube->GetFormat();
	const FIntPoint Size(RenderTargetCube->SizeX, RenderTargetCube->SizeX);
	FTextureRenderTargetCubeResource* Resource = static_cast<FTextureRenderTargetCubeResource*>(RenderTargetCube->Resource);

	if (IsHDRFormat(Format))
	{
		TArray<FFloat16Color> Pixels;
		if (!Resource->ReadPixels(Pixels, FReadSurfaceDataFlags(RCM_UNorm, CubeFace)))
		{
			return nullptr;
		}

		FaceTexture = CreateTransientTexture(Pixels.GetData(), Pixels.Num() * sizeof(FFloat16Color), Size, PF_FloatRGBA, false);
	}
	else
	{
		TArray<FColor> Pixels;
		if (!Resource->ReadPixels(Pixels, FReadSurfaceDataFlags(RCM_UNorm, CubeFace)))
		{
			return nullptr;
		}

		FaceTexture = CreateTransientTexture(Pixels.GetData(), Pixels.Num() * sizeof(FColor), Size, PF_B8G8R8A8, false);
	}

	return FaceTexture;
}

bool FGLTFTextureUtility::ReadEncodedPixels(const UTextureRenderTarget2D* InRenderTarget, TArray<FColor>& OutPixels, EGLTFJsonHDREncoding& OutEncoding)
{
	FTextureRenderTarget2DResource* Resource = static_cast<FTextureRenderTarget2DResource*>(InRenderTarget->Resource);
	if (Resource == nullptr)
	{
		return false;
	}

	if (IsHDRFormat(InRenderTarget->GetFormat()))
	{
		TArray<FLinearColor> HDRPixels;
		Resource->ReadLinearColorPixels(HDRPixels);

		EncodeRGBM(HDRPixels, OutPixels);
		OutEncoding = EGLTFJsonHDREncoding::RGBM;
	}
	else
	{
		Resource->ReadPixels(OutPixels);
		OutEncoding = EGLTFJsonHDREncoding::None;
	}

	return true;
}

FColor FGLTFTextureUtility::EncodeRGBM(const FLinearColor& Color, float MaxRange)
{
	// Based on PlayCanvas modified RGBM encoding.

	FLinearColor RGBM;

	RGBM.R = FMath::Sqrt(Color.R);
	RGBM.G = FMath::Sqrt(Color.G);
	RGBM.B = FMath::Sqrt(Color.B);

	RGBM.R /= MaxRange;
	RGBM.G /= MaxRange;
	RGBM.B /= MaxRange;

	RGBM.A = FMath::Max(FMath::Max(RGBM.R, RGBM.G), FMath::Max(RGBM.B, 1.0f / 255.0f));
	RGBM.A = FMath::CeilToFloat(RGBM.A * 255.0f) / 255.0f;

	RGBM.R /= RGBM.A;
	RGBM.G /= RGBM.A;
	RGBM.B /= RGBM.A;

	return RGBM.ToFColor(false);
}

void FGLTFTextureUtility::EncodeRGBM(const TArray<FLinearColor>& InPixels, TArray<FColor>& OutPixels, float MaxRange)
{
	OutPixels.AddUninitialized(InPixels.Num());

	for (int32 Index = 0; Index < InPixels.Num(); ++Index)
	{
		OutPixels[Index] = EncodeRGBM(InPixels[Index], MaxRange);
	}
}

bool FGLTFTextureUtility::LoadPlatformData(UTexture2D* Texture)
{
	if (Texture->PlatformData == nullptr || Texture->PlatformData->Mips.Num() == 0)
	{
		return false;
	}

	if (Texture->PlatformData->Mips[0].BulkData.GetBulkDataSize() == 0)
	{
		// TODO: is this correct handling?
		Texture->ForceRebuildPlatformData();
		if (Texture->PlatformData->Mips[0].BulkData.GetBulkDataSize() == 0)
		{
			return false;
		}
	}

	return true;
}

bool FGLTFTextureUtility::LoadPlatformData(UTextureCube* TextureCube)
{
	if (TextureCube->PlatformData == nullptr || TextureCube->PlatformData->Mips.Num() == 0)
	{
		return false;
	}

	if (TextureCube->PlatformData->Mips[0].BulkData.GetBulkDataSize() == 0)
	{
		// TODO: is this correct handling?
		TextureCube->ForceRebuildPlatformData();
		if (TextureCube->PlatformData->Mips[0].BulkData.GetBulkDataSize() == 0)
		{
			return false;
		}
	}

	return true;
}
