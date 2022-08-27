// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tasks/GLTFTextureTasks.h"
#include "Builders/GLTFContainerBuilder.h"
#include "Converters/GLTFConverterUtility.h"
#include "Converters/GLTFTextureUtility.h"

void FGLTFTexture2DTask::Complete()
{
	FGLTFJsonImageIndex ImageIndex;

	FGLTFJsonTexture& JsonTexture = Builder.GetTexture(TextureIndex);
	Texture2D->GetName(JsonTexture.Name);

	// NOTE: export of normalmaps via source data is disabled because we need to manipulate the pixels during conversion.
	const bool bPreferSourceExport = Texture2D->IsNormalMap() ? false : Builder.ExportOptions->bExportSourceTextures;

	// NOTE: export of lightmaps via source data is used to work around issues
	// with quality-loss due to incorrect gamma transformation when rendering to a canvas.

	// TODO: make sure there are no other special cases than lightmaps that require source-export
	const bool bRequireSourceExport = Texture2D->IsA<ULightMapTexture2D>();

	if (bRequireSourceExport || bPreferSourceExport)
	{
		ERGBFormat RGBFormat;
		uint32 BitDepth;

		FTextureSource& Source = const_cast<FTextureSource&>(Texture2D->Source);
		const FIntPoint Size = { Source.GetSizeX(), Source.GetSizeY() };

		if (Source.IsValid() && FGLTFTextureUtility::CanPNGCompressFormat(Source.GetFormat(), RGBFormat, BitDepth))
		{
			const void* RawData = Source.LockMip(0);
			ImageIndex = Builder.AddImage(RawData, Source.CalcMipSize(0), Size, RGBFormat, BitDepth, JsonTexture.Name);
			Source.UnlockMip(0);
		}

		if (ImageIndex != INDEX_NONE)
		{
			if (FGLTFTextureUtility::HasAnyAdjustment(Texture2D))
			{
				Builder.AddWarningMessage(FString::Printf(TEXT("Adjustments for texture %s are not supported when exporting source data"), *JsonTexture.Name));
			}
		}
		else
		{
			if (bPreferSourceExport)
			{
				Builder.AddWarningMessage(FString::Printf(TEXT("Unable to export source for texture %s, render data will be used as fallback"), *JsonTexture.Name));
			}
		}
	}

	if (ImageIndex == INDEX_NONE && !bRequireSourceExport)
	{
		const FIntPoint Size = { Texture2D->GetSizeX(), Texture2D->GetSizeY() };

		// TODO: both bForceLinearGamma and TargetGamma=2.2f seem to be necessary for the exported images to match the results
		// from exporting using the texture's source or its platform-data.
		// It's not entirely clear why gamma must be set to 2.2 instead of 0.0 (which should use the correct gamma anyway),
		// and why bInForceLinearGamma must also be true.
		const bool bForceLinearGamma = true;
		const float TargetGamma = 2.2f;

		const EPixelFormat RenderTargetFormat = FGLTFTextureUtility::IsHDRFormat(Texture2D->GetPixelFormat()) ? PF_FloatRGBA : PF_B8G8R8A8;
		UTextureRenderTarget2D* RenderTarget = FGLTFTextureUtility::CreateRenderTarget(Size, RenderTargetFormat, bForceLinearGamma);
		RenderTarget->TargetGamma = TargetGamma;

		FGLTFTextureUtility::DrawTexture(RenderTarget, Texture2D);

		TArray<FColor> Pixels;
		if (FGLTFTextureUtility::ReadEncodedPixels(RenderTarget, Pixels, JsonTexture.Encoding)) // TODO: use only encoding as specified by export options
		{
			ImageIndex = Builder.AddImage(Pixels.GetData(), Size, JsonTexture.Name);
		}
	}

	if (ImageIndex == INDEX_NONE)
	{
		Builder.AddWarningMessage(FString::Printf(TEXT("Failed to export texture %s"), *JsonTexture.Name));
		return;
	}

	JsonTexture.Source = ImageIndex;
	JsonTexture.Sampler = Builder.GetOrAddSampler(Texture2D);
}

void FGLTFTextureCubeTask::Complete()
{
	FGLTFJsonTexture& JsonTexture = Builder.GetTexture(TextureIndex);
	JsonTexture.Name = TextureCube->GetName() + TEXT("_") + FGLTFJsonUtility::ToString(FGLTFConverterUtility::ConvertCubeFace(CubeFace));

	// TODO: add optimized "happy path" if cube face doesn't need rotation and has suitable pixel format

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
	if (!FGLTFTextureUtility::ReadEncodedPixels(RenderTarget, Pixels, JsonTexture.Encoding)) // TODO: use only encoding as specified by export options
	{
		// TODO: report error
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
	if (!FGLTFTextureUtility::ReadEncodedPixels(RenderTarget2D, Pixels, JsonTexture.Encoding)) // TODO: use only encoding as specified by export options
	{
		// TODO: report error
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
	if (!FGLTFTextureUtility::ReadEncodedPixels(RenderTarget, Pixels, JsonTexture.Encoding)) // TODO: use only encoding as specified by export options
	{
		// TODO: report error
		return;
	}

	JsonTexture.Source = Builder.AddImage(Pixels.GetData(), Size, JsonTexture.Name);
	JsonTexture.Sampler = Builder.GetOrAddSampler(RenderTargetCube);
}
