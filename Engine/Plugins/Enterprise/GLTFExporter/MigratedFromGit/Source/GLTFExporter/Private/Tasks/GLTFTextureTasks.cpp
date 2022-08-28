// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tasks/GLTFTextureTasks.h"
#include "Builders/GLTFContainerBuilder.h"
#include "Converters/GLTFConverterUtility.h"
#include "Converters/GLTFTextureUtility.h"
#include "Converters/GLTFNameUtility.h"

void FGLTFTexture2DTask::Complete()
{
	FGLTFJsonTexture& JsonTexture = Builder.GetTexture(TextureIndex);
	Texture2D->GetName(JsonTexture.Name);

	// TODO: both bForceLinearGamma and TargetGamma=2.2f seem to be necessary for the exported images to match the results
	// from exporting using the texture's source or its platform-data.
	// It's not entirely clear why gamma must be set to 2.2 instead of 0.0 (which should use the correct gamma anyway),
	// and why bInForceLinearGamma must also be true.
	const bool bForceLinearGamma = true;
	const float TargetGamma = 2.2f;
	const FIntPoint Size = { Texture2D->GetSizeX(), Texture2D->GetSizeY() };

	const EPixelFormat RenderTargetFormat = FGLTFTextureUtility::IsHDRFormat(Texture2D->GetPixelFormat()) ? PF_FloatRGBA : PF_B8G8R8A8;
	UTextureRenderTarget2D* RenderTarget = FGLTFTextureUtility::CreateRenderTarget(Size, RenderTargetFormat, bForceLinearGamma);
	RenderTarget->TargetGamma = TargetGamma;

	// TODO: preserve maximum image quality (avoid compression artifacts) by copying source data (and adjustments) to a temp texture
	FGLTFTextureUtility::DrawTexture(RenderTarget, Texture2D);

	TArray<FColor> Pixels;
	if (!Texture2D->IsNormalMap() && FGLTFTextureUtility::IsHDRFormat(RenderTarget->GetFormat()))
	{
		JsonTexture.Encoding = EGLTFJsonHDREncoding::RGBM; // TODO: use only encoding as specified by export options
		FGLTFTextureUtility::ReadEncodedPixels(RenderTarget, Pixels, JsonTexture.Encoding);
	}
	else
	{
		FGLTFTextureUtility::ReadPixels(RenderTarget, Pixels);

		if (Texture2D->IsNormalMap())
		{
			FGLTFTextureUtility::FlipGreenChannel(Pixels);
		}
	}

	if (Pixels.Num() == 0)
	{
		Builder.AddWarningMessage(FString::Printf(TEXT("Failed to read pixels for 2D texture %s"), *JsonTexture.Name));
		return;
	}

	JsonTexture.Source = Builder.AddImage(Pixels.GetData(), Size, JsonTexture.Name);
	JsonTexture.Sampler = Builder.GetOrAddSampler(Texture2D);
}

void FGLTFTextureCubeTask::Complete()
{
	FGLTFJsonTexture& JsonTexture = Builder.GetTexture(TextureIndex);
	JsonTexture.Name = TextureCube->GetName() + TEXT("_") + FGLTFJsonUtility::ToString(FGLTFConverterUtility::ConvertCubeFace(CubeFace));

	// TODO: add optimized "happy path" if cube face doesn't need rotation and has suitable pixel format

	// TODO: preserve maximum image quality (avoid compression artifacts) by copying source data (and adjustments) to a temp texture
	const UTexture2D* FaceTexture = FGLTFTextureUtility::CreateTextureFromCubeFace(TextureCube, CubeFace);
	if (FaceTexture == nullptr)
	{
		// TODO: report error
		return;
	}

	const FIntPoint Size = { FaceTexture->GetSizeX(), FaceTexture->GetSizeY() };
	const EPixelFormat RenderTargetFormat = FGLTFTextureUtility::IsHDRFormat(FaceTexture->GetPixelFormat()) ? PF_FloatRGBA : PF_B8G8R8A8;
	UTextureRenderTarget2D* RenderTarget = FGLTFTextureUtility::CreateRenderTarget(Size, RenderTargetFormat, true);

	const float FaceRotation = FGLTFTextureUtility::GetCubeFaceRotation(CubeFace);
	FGLTFTextureUtility::RotateTexture(RenderTarget, FaceTexture, FaceRotation);

	TArray<FColor> Pixels;
	if (FGLTFTextureUtility::IsHDRFormat(RenderTarget->GetFormat()))
	{
		JsonTexture.Encoding = EGLTFJsonHDREncoding::RGBM; // TODO: use only encoding as specified by export options
		FGLTFTextureUtility::ReadEncodedPixels(RenderTarget, Pixels, JsonTexture.Encoding);
	}
	else
	{
		FGLTFTextureUtility::ReadPixels(RenderTarget, Pixels);
	}

	if (Pixels.Num() == 0)
	{
		Builder.AddWarningMessage(FString::Printf(TEXT("Failed to read pixels for cubemap texture %s"), *JsonTexture.Name));
		return;
	}

	JsonTexture.Source = Builder.AddImage(Pixels.GetData(), Size, JsonTexture.Name);
	JsonTexture.Sampler = Builder.GetOrAddSampler(TextureCube);
}

void FGLTFTextureRenderTarget2DTask::Complete()
{
	FGLTFJsonTexture& JsonTexture = Builder.GetTexture(TextureIndex);
	RenderTarget2D->GetName(JsonTexture.Name);

	TArray<FColor> Pixels;
	if (FGLTFTextureUtility::IsHDRFormat(RenderTarget2D->GetFormat()))
	{
		JsonTexture.Encoding = EGLTFJsonHDREncoding::RGBM; // TODO: use only encoding as specified by export options
		FGLTFTextureUtility::ReadEncodedPixels(RenderTarget2D, Pixels, JsonTexture.Encoding);
	}
	else
	{
		FGLTFTextureUtility::ReadPixels(RenderTarget2D, Pixels);
	}

	if (Pixels.Num() == 0)
	{
		Builder.AddWarningMessage(FString::Printf(TEXT("Failed to read pixels for 2D render target %s"), *JsonTexture.Name));
		return;
	}

	JsonTexture.Source = Builder.AddImage(Pixels.GetData(), { RenderTarget2D->SizeX, RenderTarget2D->SizeY }, JsonTexture.Name);
	JsonTexture.Sampler = Builder.GetOrAddSampler(RenderTarget2D);
}

void FGLTFTextureRenderTargetCubeTask::Complete()
{
	FGLTFJsonTexture& JsonTexture = Builder.GetTexture(TextureIndex);
	JsonTexture.Name = RenderTargetCube->GetName() + TEXT("_") + FGLTFJsonUtility::ToString(FGLTFConverterUtility::ConvertCubeFace(CubeFace));

	// TODO: add optimized "happy path" if cube face doesn't need rotation

	const UTexture2D* FaceTexture = FGLTFTextureUtility::CreateTextureFromCubeFace(RenderTargetCube, CubeFace);
	if (FaceTexture == nullptr)
	{
		// TODO: report error
		return;
	}

	const FIntPoint Size = { FaceTexture->GetSizeX(), FaceTexture->GetSizeY() };
	const EPixelFormat RenderTargetFormat = FGLTFTextureUtility::IsHDRFormat(FaceTexture->GetPixelFormat()) ? PF_FloatRGBA : PF_B8G8R8A8;
	UTextureRenderTarget2D* RenderTarget = FGLTFTextureUtility::CreateRenderTarget(Size, RenderTargetFormat, true);

	const float FaceRotation = FGLTFTextureUtility::GetCubeFaceRotation(CubeFace);
	FGLTFTextureUtility::RotateTexture(RenderTarget, FaceTexture, FaceRotation);

	TArray<FColor> Pixels;
	if (FGLTFTextureUtility::IsHDRFormat(RenderTarget->GetFormat()))
	{
		JsonTexture.Encoding = EGLTFJsonHDREncoding::RGBM; // TODO: use only encoding as specified by export options
		FGLTFTextureUtility::ReadEncodedPixels(RenderTarget, Pixels, JsonTexture.Encoding);
	}
	else
	{
		FGLTFTextureUtility::ReadPixels(RenderTarget, Pixels);
	}

	if (Pixels.Num() == 0)
	{
		Builder.AddWarningMessage(FString::Printf(TEXT("Failed to read pixels for cubemap render target %s"), *JsonTexture.Name));
		return;
	}

	JsonTexture.Source = Builder.AddImage(Pixels.GetData(), Size, JsonTexture.Name);
	JsonTexture.Sampler = Builder.GetOrAddSampler(RenderTargetCube);
}

void FGLTFTextureLightMapTask::Complete()
{
	FGLTFJsonTexture& JsonTexture = Builder.GetTexture(TextureIndex);
	LightMap->GetName(JsonTexture.Name);

	// NOTE: export of lightmaps via source data is used to work around issues with
	// quality-loss due to incorrect gamma transformation when rendering to a canvas.

	FTextureSource& Source = const_cast<FTextureSource&>(LightMap->Source);
	if (!Source.IsValid())
	{
		Builder.AddWarningMessage(FString::Printf(TEXT("Failed to export lightmap texture %s because of missing source data"), *JsonTexture.Name));
		return;
	}

	ERGBFormat RGBFormat;
	uint32 BitDepth;
	if (!FGLTFTextureUtility::CanPNGCompressFormat(Source.GetFormat(), RGBFormat, BitDepth))
	{
		Builder.AddWarningMessage(FString::Printf(TEXT("Failed to export lightmap texture %s because of unsupported format %s"), *JsonTexture.Name, *FGLTFNameUtility::GetName(Source.GetFormat())));
		return;
	}

	const FIntPoint Size = { Source.GetSizeX(), Source.GetSizeY() };
	const void* RawData = Source.LockMip(0);
	JsonTexture.Source = Builder.AddImage(RawData, Source.CalcMipSize(0), Size, RGBFormat, BitDepth, JsonTexture.Name);
	Source.UnlockMip(0);

	JsonTexture.Sampler = Builder.GetOrAddSampler(LightMap);
}
