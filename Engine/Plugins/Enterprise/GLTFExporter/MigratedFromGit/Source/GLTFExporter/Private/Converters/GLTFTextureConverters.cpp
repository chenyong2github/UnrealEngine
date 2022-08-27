// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFTextureConverters.h"
#include "Builders/GLTFContainerBuilder.h"
#include "Converters/GLTFConverterUtility.h"

FGLTFJsonSamplerIndex FGLTFTextureSamplerConverter::Add(FGLTFConvertBuilder& Builder, const FString& Name, const UTexture* Texture)
{
	// TODO: maybe we should reuse existing samplers?

	FGLTFJsonSampler JsonSampler;
	JsonSampler.Name = Name;

	JsonSampler.MinFilter = FGLTFConverterUtility::ConvertMinFilter(Texture->Filter, Texture->LODGroup);
	JsonSampler.MagFilter = FGLTFConverterUtility::ConvertMagFilter(Texture->Filter, Texture->LODGroup);

	if (const UTexture2D* Texture2D = Cast<UTexture2D>(Texture))
	{
		JsonSampler.WrapS = FGLTFConverterUtility::ConvertWrap(Texture2D->AddressX);
		JsonSampler.WrapT = FGLTFConverterUtility::ConvertWrap(Texture2D->AddressY);
	}
	else if (const UTextureRenderTarget2D* RenderTarget2D = Cast<UTextureRenderTarget2D>(Texture))
	{
		JsonSampler.WrapS = FGLTFConverterUtility::ConvertWrap(RenderTarget2D->AddressX);
		JsonSampler.WrapT = FGLTFConverterUtility::ConvertWrap(RenderTarget2D->AddressY);
	}
	else
	{
		// TODO: handle this?
	}

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
	// TODO: maybe we should reuse existing samplers?
	FGLTFJsonSampler JsonSampler;
	JsonSampler.Name = Name;

	// TODO: are these filters ok to use as default? The actual texture uses "nearest"
	JsonSampler.MinFilter = EGLTFJsonTextureFilter::LinearMipmapLinear;
	JsonSampler.MagFilter = EGLTFJsonTextureFilter::Linear;

	JsonSampler.WrapS = FGLTFConverterUtility::ConvertWrap(LightMapTexture2D->AddressX);
	JsonSampler.WrapT = FGLTFConverterUtility::ConvertWrap(LightMapTexture2D->AddressY);

	FGLTFJsonTexture JsonTexture;
	JsonTexture.Name = Name;
	JsonTexture.Sampler = Builder.AddSampler(JsonSampler);
	JsonTexture.Source = Builder.AddImage(LightMapTexture2D->Source, Name);

	return Builder.AddTexture(JsonTexture);
}
