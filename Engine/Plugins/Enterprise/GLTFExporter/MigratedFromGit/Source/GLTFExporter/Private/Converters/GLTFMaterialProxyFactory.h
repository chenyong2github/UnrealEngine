// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_EDITOR

#include "Builders/GLTFConvertBuilder.h"
#include "Options/GLTFProxyOptions.h"
#include "Options/GLTFExportOptions.h"

class UMaterialInstanceConstant;

class FGLTFMaterialProxyFactory
{
public:

	FGLTFMaterialProxyFactory(const UGLTFProxyOptions* Options = nullptr);

	UMaterialInterface* Create(UMaterialInterface* OriginalMaterial);
	void OpenLog();

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

	void SetUserData(UMaterialInstanceConstant* ProxyMaterial, UMaterialInterface* OriginalMaterial);
	void SetBaseProperties(UMaterialInstanceConstant* ProxyMaterial, UMaterialInterface* OriginalMaterial);
	void SetProxyProperties(UMaterialInstanceConstant* ProxyMaterial, const FGLTFJsonMaterial& JsonMaterial);

	void SetProxyProperty(UMaterialInstanceConstant* ProxyMaterial, const FString& PropertyName, float Scalar);
	void SetProxyProperty(UMaterialInstanceConstant* ProxyMaterial, const FString& PropertyName, const FGLTFJsonColor3& Color);
	void SetProxyProperty(UMaterialInstanceConstant* ProxyMaterial, const FString& PropertyName, const FGLTFJsonColor4& Color);
	void SetProxyProperty(UMaterialInstanceConstant* ProxyMaterial, const FString& PropertyName, const FGLTFJsonTextureInfo& TextureInfo, EGLTFMaterialPropertyGroup PropertyGroup);

	UTexture2D* FindOrCreateTexture(FGLTFJsonTextureIndex Index, EGLTFMaterialPropertyGroup PropertyGroup);
	UTexture2D* CreateTexture(const FGLTFImageData* ImageData, const FGLTFJsonSampler& JsonSampler, EGLTFMaterialPropertyGroup PropertyGroup);

	UMaterialInstanceConstant* CreateInstancedMaterial(UMaterialInterface* OriginalMaterial, EGLTFJsonShadingModel ShadingModel);

	UPackage* FindOrCreatePackage(const FString& BaseName);

	TUniquePtr<IGLTFImageConverter> CreateCustomImageConverter();

	static bool MakeDirectory(const FString& String);

	static UGLTFExportOptions* CreateExportOptions(const UGLTFProxyOptions* ProxyOptions);

	static TextureAddress ConvertWrap(EGLTFJsonTextureWrap Wrap);
	static TextureFilter ConvertFilter(EGLTFJsonTextureFilter Filter);

	FGLTFConvertBuilder Builder;

	TMap<FGLTFJsonTextureIndex, UTexture2D*> Textures;
	TMap<FGLTFJsonImageIndex, FGLTFImageData> Images;
};

#endif
