// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Builders/GLTFContainerBuilder.h"
#include "Engine/Texture2D.h"

struct FGLTFTextureCombineSource
{
	constexpr FORCEINLINE FGLTFTextureCombineSource(const UTexture2D* Texture, FLinearColor TintColor = FLinearColor(1, 1, 1, 1), ESimpleElementBlendMode BlendMode = SE_BLEND_Additive)
		: Texture(Texture), TintColor(TintColor), BlendMode(BlendMode)
	{}

	const UTexture2D* Texture;
	FLinearColor TintColor;
	ESimpleElementBlendMode BlendMode;
};

struct FGLTFMaterialUtility
{
	static UTexture2D* CreateTransientTexture(const TArray<FColor>& Pixels, const FIntPoint& TextureSize, const EPixelFormat& TextureFormat);
	static bool CombineTextures(TArray<FColor>& OutPixels, const TArray<FGLTFTextureCombineSource>& Sources, const FIntPoint& OutputSize, const EPixelFormat OutputPixelFormat);
	static UTexture2D* BakeMaterialProperty(const FIntPoint OutputSize, const EMaterialProperty& MaterialProperty, const UMaterialInterface* Material);

	static FGLTFJsonTextureIndex AddCombinedTexture(FGLTFConvertBuilder& Builder, const TArray<FGLTFTextureCombineSource>& CombineSources, const FIntPoint TextureSize, const FString& TextureName, const EGLTFJsonTextureFilter Filter, const EGLTFJsonTextureWrap Wrap);
	static FIntVector4 ConvertMaskToVector(const FExpressionInput& ExpressionInput);
	static FLinearColor ConvertMaskToColor(const FExpressionInput& ExpressionInput);
	static uint32 GetMaskComponentCount(const FExpressionInput& ExpressionInput);
};
