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

	const TextureAddress AddressX = FGLTFTextureUtility::GetAddressX(Texture);
	const TextureAddress AddressY = FGLTFTextureUtility::GetAddressY(Texture);

	// TODO: report error if AddressX == TA_MAX || AddressY == TA_MAX

	JsonSampler.WrapS = FGLTFConverterUtility::ConvertWrap(AddressX);
	JsonSampler.WrapT = FGLTFConverterUtility::ConvertWrap(AddressY);

	return Builder.AddSampler(JsonSampler);
}

FGLTFJsonTextureIndex FGLTFTexture2DConverter::Add(FGLTFConvertBuilder& Builder, const FString& Name, const UTexture2D* Texture2D)
{
	// TODO: add RGBE encoding information for TSF_RGBE8 and TSF_BGRE8

	FGLTFJsonTexture JsonTexture;
	JsonTexture.Name = Name;
	JsonTexture.Sampler = Builder.GetOrAddSampler(Texture2D);
	JsonTexture.Source = Builder.AddImage(Texture2D->Source, Name);

	return Builder.AddTexture(JsonTexture);
}

FGLTFJsonTextureIndex FGLTFTextureCubeConverter::Add(FGLTFConvertBuilder& Builder, const FString& Name, const UTextureCube* TextureCube)
{
	// TODO: implement support
	return FGLTFJsonTextureIndex(INDEX_NONE);
}

FGLTFJsonTextureIndex FGLTFLightMapTexture2DConverter::Add(FGLTFConvertBuilder& Builder, const FString& Name, const ULightMapTexture2D* LightMapTexture2D)
{
	// TODO: add RGBE encoding information for TSF_RGBE8 and TSF_BGRE8

	FGLTFJsonTexture JsonTexture;
	JsonTexture.Name = Name;
	JsonTexture.Sampler = Builder.GetOrAddSampler(LightMapTexture2D);
	JsonTexture.Source = Builder.AddImage(LightMapTexture2D->Source, Name);

	return Builder.AddTexture(JsonTexture);
}
