// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_EDITOR

#include "Builders/GLTFConvertBuilder.h"
#include "Options/GLTFPrebakeOptions.h"
#include "Options/GLTFExportOptions.h"

class UMaterialInstanceConstant;

class FGLTFMaterialPrebaker
{
public:

	FGLTFMaterialPrebaker(const UGLTFPrebakeOptions* Options = nullptr);

	UMaterialInterface* Prebake(UMaterialInterface* OriginalMaterial);

	FString RootPath;

private:

	struct FGLTFImageData
	{
		FString Filename;
		EGLTFTextureType Type;
		bool bIgnoreAlpha;
		FIntPoint Size;
		TGLTFSharedArray<FColor> Pixels;
	};

	void ApplyPrebakedProperties(UMaterialInstanceConstant* ProxyMaterial, const FGLTFJsonMaterial& JsonMaterial);

	void ApplyPrebakedProperty(UMaterialInstanceConstant* ProxyMaterial, const FString& PropertyName, float Scalar);
	void ApplyPrebakedProperty(UMaterialInstanceConstant* ProxyMaterial, const FString& PropertyName, const FGLTFJsonColor3& Color);
	void ApplyPrebakedProperty(UMaterialInstanceConstant* ProxyMaterial, const FString& PropertyName, const FGLTFJsonColor4& Color);
	void ApplyPrebakedProperty(UMaterialInstanceConstant* ProxyMaterial, const FString& PropertyName, const FGLTFJsonTextureInfo& TextureInfo, bool bNormalMap = false);

	const UTexture2D* FindOrCreateTexture(FGLTFJsonTextureIndex Index, bool bNormalMap);
	UTexture2D* CreateTexture(const FGLTFImageData* ImageData, const FGLTFJsonSampler& JsonSampler, bool bNormalMap);

	UMaterialInstanceConstant* CreateProxyMaterial(UMaterialInterface* OriginalMaterial, EGLTFJsonShadingModel ShadingModel);

	TUniquePtr<IGLTFImageConverter> CreateCustomImageConverter();

	static UGLTFExportOptions* CreateExportOptions(const UGLTFPrebakeOptions* PrebakeOptions);

	static TextureAddress ConvertWrap(EGLTFJsonTextureWrap Wrap);
	static TextureFilter ConvertFilter(EGLTFJsonTextureFilter Filter);

	FGLTFConvertBuilder Builder;

	TMap<FGLTFJsonTextureIndex, const UTexture2D*> Textures;
	TMap<FGLTFJsonImageIndex, FGLTFImageData> Images;
};

#endif
