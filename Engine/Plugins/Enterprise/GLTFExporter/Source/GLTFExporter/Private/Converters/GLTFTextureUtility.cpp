// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFTextureUtility.h"
#include "Converters/GLTFNormalMapPreview.h"
#include "Converters/GLTFSimpleTexture2DPreview.h"
#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "TextureCompiler.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureCube.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/TextureRenderTargetCube.h"
#include "DeviceProfiles/DeviceProfile.h"
#include "DeviceProfiles/DeviceProfileManager.h"
#include "TextureResource.h"

bool FGLTFTextureUtility::IsAlphaless(EPixelFormat PixelFormat)
{
	switch (PixelFormat)
	{
		case PF_ATC_RGB:
		case PF_BC4:
		case PF_BC5:
		case PF_DXT1:
		case PF_ETC1:
		case PF_ETC2_RGB:
		case PF_FloatR11G11B10:
		case PF_FloatRGB:
		case PF_R5G6B5_UNORM:
			// TODO: add more pixel formats that don't support alpha, but beware of formats like PF_G8 (that still seem to return alpha in some cases)
			return true;
		default:
			return false;
	}
}

void FGLTFTextureUtility::FullyLoad(const UTexture* InTexture)
{
	UTexture* Texture = const_cast<UTexture*>(InTexture);

#if WITH_EDITOR
	FTextureCompilingManager::Get().FinishCompilation({ Texture });
#endif

	Texture->SetForceMipLevelsToBeResident(30.0f);
	Texture->WaitForStreaming();
}

bool FGLTFTextureUtility::IsHDR(const UTexture* Texture)
{
	switch (Texture->CompressionSettings)
	{
		case TC_HDR:
		case TC_HDR_Compressed:
		case TC_HalfFloat:
			return true;
		default:
			return false;
	}
}

bool FGLTFTextureUtility::IsCubemap(const UTexture* Texture)
{
	return Texture->IsA<UTextureCube>() || Texture->IsA<UTextureRenderTargetCube>();
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
	const UTextureLODSettings* TextureLODSettings = UDeviceProfileManager::Get().GetActiveProfile()->GetTextureLODSettings();
	const ETextureSamplerFilter Filter = TextureLODSettings->GetTextureLODGroup(LODGroup).Filter;

	switch (Filter)
	{
		case ETextureSamplerFilter::Point:             return TF_Nearest;
		case ETextureSamplerFilter::Bilinear:          return TF_Bilinear;
		case ETextureSamplerFilter::Trilinear:         return TF_Trilinear;
		case ETextureSamplerFilter::AnisotropicPoint:  return TF_Trilinear; // A lot of engine code doesn't result in nearest
		case ETextureSamplerFilter::AnisotropicLinear: return TF_Trilinear;
		default:                                       return TF_Default; // Let caller decide fallback
	}
}

int32 FGLTFTextureUtility::GetMipBias(const UTexture* Texture)
{
	if (const UTexture2D* Texture2D = Cast<UTexture2D>(Texture))
	{
		return Texture2D->GetNumMips() - Texture2D->GetNumMipsAllowed(true);
	}

	return Texture->GetCachedLODBias();
}

FIntPoint FGLTFTextureUtility::GetInGameSize(const UTexture* Texture)
{
	const int32 Width = Texture->GetSurfaceWidth();
	const int32 Height = Texture->GetSurfaceHeight();

	const int32 MipBias = GetMipBias(Texture);

	const int32 InGameWidth = FMath::Max(Width >> MipBias, 1);
	const int32 InGameHeight = FMath::Max(Height >> MipBias, 1);

	return { InGameWidth, InGameHeight };
}

TTuple<TextureAddress, TextureAddress> FGLTFTextureUtility::GetAddressXY(const UTexture* Texture)
{
	TextureAddress AddressX;
	TextureAddress AddressY;

	if (const UTexture2D* Texture2D = Cast<UTexture2D>(Texture))
	{
		AddressX = Texture2D->AddressX;
		AddressY = Texture2D->AddressY;
	}
	else if (const UTextureRenderTarget2D* RenderTarget2D = Cast<UTextureRenderTarget2D>(Texture))
	{
		AddressX = RenderTarget2D->AddressX;
		AddressY = RenderTarget2D->AddressY;
	}
	else
	{
		AddressX = TA_MAX;
		AddressY = TA_MAX;
	}

	return MakeTuple(AddressX, AddressY);
}

UTexture2D* FGLTFTextureUtility::CreateTransientTexture(const void* RawData, int64 ByteLength, const FIntPoint& Size, EPixelFormat Format, bool bSRGB)
{
	check(CalculateImageBytes(Size.X, Size.Y, 0, Format) == ByteLength);

	// TODO: do these temp textures need to be part of the root set to avoid garbage collection?
	UTexture2D* Texture = UTexture2D::CreateTransient(Size.X, Size.Y, Format);

	void* MipData = Texture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(MipData, RawData, ByteLength);
	Texture->GetPlatformData()->Mips[0].BulkData.Unlock();

	Texture->SRGB = bSRGB ? 1 : 0;
	Texture->CompressionSettings = TC_VectorDisplacementmap; // best quality
#if WITH_EDITOR
	Texture->CompressionNone = true;
	Texture->MipGenSettings = TMGS_NoMipmaps;
#endif

	Texture->UpdateResource();
	return Texture;
}

UTextureRenderTarget2D* FGLTFTextureUtility::CreateRenderTarget(const FIntPoint& Size, bool bHDR)
{
	// TODO: instead of PF_FloatRGBA (i.e. RTF_RGBA16f) use PF_A32B32G32R32F (i.e. RTF_RGBA32f) to avoid accuracy loss
	const EPixelFormat PixelFormat = bHDR ? PF_FloatRGBA : PF_B8G8R8A8;

	// NOTE: both bForceLinearGamma and TargetGamma=2.2 seem necessary for exported images to match their source data.
	// It's not entirely clear why gamma must be 2.2 (instead of 0.0) and why bInForceLinearGamma must also be true.
	const bool bForceLinearGamma = true;
	const float TargetGamma = 2.2f;

	UTextureRenderTarget2D* RenderTarget = NewObject<UTextureRenderTarget2D>();
	RenderTarget->InitCustomFormat(Size.X, Size.Y, PixelFormat, bForceLinearGamma);

	RenderTarget->TargetGamma = TargetGamma;
	return RenderTarget;
}

bool FGLTFTextureUtility::DrawTexture(UTextureRenderTarget2D* OutTarget, const UTexture2D* InSource, const FVector2D& InPosition, const FVector2D& InSize, const FMatrix& InTransform)
{
	FRenderTarget* RenderTarget = OutTarget->GameThread_GetRenderTargetResource();
	if (RenderTarget == nullptr)
	{
		return false;
	}

	TRefCountPtr<FBatchedElementParameters> BatchedElementParameters;

	if (InSource->IsNormalMap())
	{
		BatchedElementParameters = new FGLTFNormalMapPreview();
	}
	else if (IsHDR(InSource))
	{
		// NOTE: Simple preview parameters are used to prevent any modifications
		// such as gamma-correction from being applied during rendering.
		BatchedElementParameters = new FGLTFSimpleTexture2DPreview();
	}

	FCanvas Canvas(RenderTarget, nullptr, FGameTime::CreateDilated(0.0f, 0.0f, 0.0f, 0.0f), GMaxRHIFeatureLevel);
	FCanvasTileItem TileItem(InPosition, InSource->GetResource(), InSize, FLinearColor::White);
	TileItem.BatchedElementParameters = BatchedElementParameters;

	Canvas.PushAbsoluteTransform(InTransform);
	TileItem.Draw(&Canvas);
	Canvas.PopTransform();

	Canvas.Flush_GameThread();
	FlushRenderingCommands();
	Canvas.SetRenderTarget_GameThread(nullptr);
	FlushRenderingCommands();

	return true;
}

bool FGLTFTextureUtility::RotateTexture(UTextureRenderTarget2D* OutTarget, const UTexture2D* InSource, const FVector2D& InPosition, const FVector2D& InSize, float InDegrees)
{
	FMatrix Transform = FMatrix::Identity;
	if (InDegrees != 0)
	{
		const FVector Center = FVector(InSize.X / 2.0f, InSize.Y / 2.0f, 0);
		Transform = FTranslationMatrix(-Center) * FRotationMatrix({ 0, InDegrees, 0 }) * FTranslationMatrix(Center);
	}

	return DrawTexture(OutTarget, InSource, InPosition, InSize, Transform);
}

bool FGLTFTextureUtility::ReadPixels(const UTextureRenderTarget2D* InRenderTarget, TArray<FColor>& OutPixels)
{
	FTextureRenderTarget2DResource* Resource = static_cast<FTextureRenderTarget2DResource*>(const_cast<UTextureRenderTarget2D*>(InRenderTarget)->GetResource());
	if (Resource == nullptr)
	{
		return false;
	}

	FReadSurfaceDataFlags ReadSurfaceDataFlags(RCM_UNorm, CubeFace_MAX);
	ReadSurfaceDataFlags.SetLinearToGamma(false);
	return Resource->ReadPixels(OutPixels, ReadSurfaceDataFlags);
}

void FGLTFTextureUtility::FlipGreenChannel(TArray<FColor>& Pixels)
{
	for (FColor& Pixel: Pixels)
	{
		Pixel.G = 255 - Pixel.G;
	}
}

void FGLTFTextureUtility::TransformColorSpace(TArray<FColor>& Pixels, bool bFromSRGB, bool bToSRGB)
{
	if (bFromSRGB == bToSRGB)
	{
		return;
	}

	if (bToSRGB)
	{
		for (FColor& Pixel: Pixels)
		{
			Pixel = Pixel.ReinterpretAsLinear().ToFColor(true);
		}
	}
	else
	{
		for (FColor& Pixel: Pixels)
		{
			Pixel = FLinearColor(Pixel).ToFColor(false);
		}
	}
}
