// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFTextureConverters.h"
#include "Builders/GLTFContainerBuilder.h"
#include "Converters/GLTFConverterUtility.h"
#include "Converters/GLTFTextureUtility.h"

FGLTFJsonSamplerIndex FGLTFTextureSamplerConverter::Add(FGLTFConvertBuilder& Builder, const FString& Name, const UTexture* Texture)
{
	// TODO: maybe we should reuse existing samplers?

	FGLTFJsonSampler JsonSampler;
	JsonSampler.Name = Name;

	if (Texture->IsA<ULightMapTexture2D>())
	{
		// Override default filter settings for LightMap textures (which otherwise is "nearest")
		JsonSampler.MinFilter = EGLTFJsonTextureFilter::LinearMipmapLinear;
		JsonSampler.MagFilter = EGLTFJsonTextureFilter::Linear;
	}
	else
	{
		JsonSampler.MinFilter = FGLTFConverterUtility::ConvertMinFilter(Texture->Filter, Texture->LODGroup);
		JsonSampler.MagFilter = FGLTFConverterUtility::ConvertMagFilter(Texture->Filter, Texture->LODGroup);
	}

	if (FGLTFTextureUtility::IsCubemap(Texture))
	{
		JsonSampler.WrapS = EGLTFJsonTextureWrap::ClampToEdge;
		JsonSampler.WrapT = EGLTFJsonTextureWrap::ClampToEdge;
	}
	else
	{
		const TTuple<TextureAddress, TextureAddress> AddressXY = FGLTFTextureUtility::GetAddressXY(Texture);
		// TODO: report error if AddressX == TA_MAX or AddressY == TA_MAX

		JsonSampler.WrapS = FGLTFConverterUtility::ConvertWrap(AddressXY.Get<0>());
		JsonSampler.WrapT = FGLTFConverterUtility::ConvertWrap(AddressXY.Get<1>());
	}

	return Builder.AddSampler(JsonSampler);
}

FGLTFJsonTextureIndex FGLTFTexture2DConverter::Add(FGLTFConvertBuilder& Builder, const FString& Name, const UTexture2D* Texture2D)
{
	FGLTFJsonImageIndex ImageIndex;

	FGLTFJsonTexture JsonTexture;
	JsonTexture.Name = Name;

	const FIntPoint Size = { Texture2D->GetSizeX(), Texture2D->GetSizeY() };
	ERGBFormat RGBFormat;
	uint32 BitDepth;

	if (Texture2D->IsA<ULightMapTexture2D>() || Texture2D->IsNormalMap())
	{
		// NOTE: export of light- and normal-maps via source is done to overcome issues with accessing
		// pixel-data (for light-maps) and compressed pixel-data (for normal-maps).
		// TODO: by exporting via source, we lose the ability to export texture-adjustments. We may want to support this later.

		if (FGLTFTextureUtility::CanPNGCompressFormat(Texture2D->Source.GetFormat(), RGBFormat, BitDepth))
		{
			FTextureSource& Source = const_cast<FTextureSource&>(Texture2D->Source);

			const void* RawData = Source.LockMip(0);
			ImageIndex = Builder.AddImage(RawData, Source.CalcMipSize(0), Size, RGBFormat, BitDepth, JsonTexture.Name);
			Source.UnlockMip(0);
		}
		else
		{
			// TODO: report error
			return FGLTFJsonTextureIndex(INDEX_NONE);
		}
	}
	else if (FGLTFTextureUtility::CanPNGCompressFormat(Texture2D->GetPixelFormat(), RGBFormat, BitDepth))
	{
		if (Texture2D->PlatformData->Mips[0].BulkData.GetBulkDataSize() == 0)
		{
			// TODO: is this correct handling?
			const_cast<UTexture2D*>(Texture2D)->ForceRebuildPlatformData();
			if (Texture2D->PlatformData->Mips[0].BulkData.GetBulkDataSize() == 0)
			{
				// TODO: report error
				return FGLTFJsonTextureIndex(INDEX_NONE);
			}
		}

		const FByteBulkData& BulkData = Texture2D->PlatformData->Mips[0].BulkData;
		const void* RawData = BulkData.LockReadOnly();

		ImageIndex = Builder.AddImage(RawData, BulkData.GetBulkDataSize(), Size, RGBFormat, BitDepth, JsonTexture.Name);
		BulkData.Unlock();
	}
	else
	{
		const EPixelFormat RenderTargetFormat = FGLTFTextureUtility::IsHDRFormat(Texture2D->GetPixelFormat()) ? PF_FloatRGBA : PF_B8G8R8A8;

		// TODO: both bInForceLinearGamma and TargetGamma=2.2f seem to be necessary for the exported images to match the results
		// from exporting using the texture's source or its platform-data.
		// It's not entirely clear why gamma must be set to 2.2 instead of 0.0 (which should use the correct gamma anyway),
		// and why bInForceLinearGamma must also be true.
		UTextureRenderTarget2D* RenderTarget = FGLTFTextureUtility::CreateRenderTarget(Size, RenderTargetFormat, true);
		RenderTarget->TargetGamma = 2.2f;

		FGLTFTextureUtility::DrawTexture(RenderTarget, Texture2D);

		TArray<FColor> Pixels;
		if (!FGLTFTextureUtility::ReadEncodedPixels(RenderTarget, Pixels, JsonTexture.Encoding)) // TODO: use only encoding as specified by export options
		{
			// TODO: report error
			return FGLTFJsonTextureIndex(INDEX_NONE);
		}

		ImageIndex = Builder.AddImage(Pixels.GetData(), Size, JsonTexture.Name);
	}

	JsonTexture.Source = ImageIndex;
	JsonTexture.Sampler = Builder.GetOrAddSampler(Texture2D);

	return Builder.AddTexture(JsonTexture);
}

FGLTFJsonTextureIndex FGLTFTextureCubeConverter::Add(FGLTFConvertBuilder& Builder, const FString& Name, const UTextureCube* TextureCube, ECubeFace CubeFace)
{
	FGLTFJsonTexture JsonTexture;
	JsonTexture.Name = Name;

	// TODO: add optimized "happy path" if cube face doesn't need rotation and has suitable pixel format

	const UTexture2D* FaceTexture = FGLTFTextureUtility::CreateTextureFromCubeFace(TextureCube, CubeFace);
	if (FaceTexture == nullptr)
	{
		// TODO: report error
		return FGLTFJsonTextureIndex(INDEX_NONE);
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
		return FGLTFJsonTextureIndex(INDEX_NONE);
	}

	JsonTexture.Source = Builder.AddImage(Pixels.GetData(), Size, JsonTexture.Name);
	JsonTexture.Sampler = Builder.GetOrAddSampler(TextureCube);

	return Builder.AddTexture(JsonTexture);
}

FGLTFJsonTextureIndex FGLTFTextureRenderTarget2DConverter::Add(FGLTFConvertBuilder& Builder, const FString& Name, const UTextureRenderTarget2D* RenderTarget2D)
{
	FGLTFJsonTexture JsonTexture;
	JsonTexture.Name = Name;

	TArray<FColor> Pixels;
	if (!FGLTFTextureUtility::ReadEncodedPixels(RenderTarget2D, Pixels, JsonTexture.Encoding)) // TODO: use only encoding as specified by export options
	{
		// TODO: report error
		return FGLTFJsonTextureIndex(INDEX_NONE);
	}

	JsonTexture.Source = Builder.AddImage(Pixels.GetData(), { RenderTarget2D->SizeX, RenderTarget2D->SizeY }, JsonTexture.Name);
	JsonTexture.Sampler = Builder.GetOrAddSampler(RenderTarget2D);

	return Builder.AddTexture(JsonTexture);
}

FGLTFJsonTextureIndex FGLTFTextureRenderTargetCubeConverter::Add(FGLTFConvertBuilder& Builder, const FString& Name, const UTextureRenderTargetCube* RenderTargetCube, ECubeFace CubeFace)
{
	FGLTFJsonTexture JsonTexture;
	JsonTexture.Name = Name;

	// TODO: add optimized "happy path" if cube face doesn't need rotation

	const UTexture2D* FaceTexture = FGLTFTextureUtility::CreateTextureFromCubeFace(RenderTargetCube, CubeFace);
	if (FaceTexture == nullptr)
	{
		// TODO: report error
		return FGLTFJsonTextureIndex(INDEX_NONE);
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
		return FGLTFJsonTextureIndex(INDEX_NONE);
	}

	JsonTexture.Source = Builder.AddImage(Pixels.GetData(), Size, JsonTexture.Name);
	JsonTexture.Sampler = Builder.GetOrAddSampler(RenderTargetCube);

	return Builder.AddTexture(JsonTexture);
}
