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

	FIntPoint Size = { Texture2D->GetSizeX(), Texture2D->GetSizeY() };
	ERGBFormat RGBFormat;
	uint32 BitDepth;

	if (FGLTFTextureUtility::CanPNGCompressFormat(Texture2D->Source.GetFormat(), RGBFormat, BitDepth))
	{
		FTextureSource& Source = const_cast<FTextureSource&>(Texture2D->Source);

		const void* RawData = Source.LockMip(0);
		ImageIndex = Builder.AddImage(RawData, Source.CalcMipSize(0), Size, RGBFormat, BitDepth, JsonTexture.Name);
		Source.UnlockMip(0);
	}
	else if (FGLTFTextureUtility::CanPNGCompressFormat(Texture2D->GetPixelFormat(), RGBFormat, BitDepth))
	{
		const FByteBulkData& BulkData = Texture2D->PlatformData->Mips[0].BulkData;

		const void* RawData = BulkData.LockReadOnly();
		ImageIndex = Builder.AddImage(RawData, BulkData.GetBulkDataSize(), Size, RGBFormat, BitDepth, JsonTexture.Name);
		BulkData.Unlock();
	}
	else
	{
		UTextureRenderTarget2D* RenderTarget = FGLTFTextureUtility::CreateRenderTarget(Size, PF_B8G8R8A8, true);

		FGLTFTextureUtility::DrawTexture(RenderTarget, Texture2D);

		FTextureRenderTarget2DResource* Resource = static_cast<FTextureRenderTarget2DResource*>(RenderTarget->Resource);
		if (Resource == nullptr)
		{
			// TODO: report error
			return FGLTFJsonTextureIndex(INDEX_NONE);
		}

		TArray<FColor> Pixels;
		if (!Resource->ReadPixels(Pixels))
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

FGLTFJsonTextureIndex FGLTFTextureCubeConverter::Add(FGLTFConvertBuilder& Builder, const FString& Name, const UTextureCube* TextureCube)
{
	// TODO: implement support
	return FGLTFJsonTextureIndex(INDEX_NONE);
}
