// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Builders/GLTFContainerBuilder.h"
#include "Engine/Texture2D.h"

struct FGLTFTextureCombineSource
{
	constexpr FORCEINLINE FGLTFTextureCombineSource(const UTexture2D* Texture, FLinearColor TintColor = { 1.0f, 1.0f, 1.0f, 1.0f }, ESimpleElementBlendMode BlendMode = SE_BLEND_Additive)
		: Texture(Texture), TintColor(TintColor), BlendMode(BlendMode)
	{}

	const UTexture2D* Texture;
	FLinearColor TintColor;
	ESimpleElementBlendMode BlendMode;
};

struct FGLTFPropertyBakeOutput
{
	FORCEINLINE FGLTFPropertyBakeOutput(EMaterialProperty Property, EPixelFormat PixelFormat, TArray<FColor>& Pixels, FIntPoint Size, float EmissiveScale)
		: Property(Property), PixelFormat(PixelFormat), Pixels(Pixels), Size(Size), EmissiveScale(EmissiveScale), bIsConstant(false)
	{}

	EMaterialProperty Property;
	EPixelFormat PixelFormat;
	TArray<FColor> Pixels;
	FIntPoint Size;
	float EmissiveScale;

	bool bIsConstant;
	FLinearColor ConstantValue;
};

struct FGLTFMaterialUtility
{
	static FVector4 GetPropertyDefaultValue(EMaterialProperty Property);

	static const FExpressionInput* GetInputFromProperty(const UMaterialInterface* Material, EMaterialProperty Property);

	template<class InputType>
    static const FMaterialInput<InputType>* GetInputFromProperty(const UMaterialInterface* Material, EMaterialProperty Property)
	{
		const FExpressionInput* ExpressionInput = const_cast<UMaterial*>(Material->GetMaterial())->GetExpressionInputForProperty(Property);
		return static_cast<const FMaterialInput<InputType>*>(ExpressionInput);
	}

	static UTexture2D* CreateTransientTexture(const FGLTFPropertyBakeOutput& PropertyBakeOutput, bool bUseSRGB = false);
	static UTexture2D* CreateTransientTexture(const TArray<FColor>& Pixels, const FIntPoint& TextureSize, EPixelFormat TextureFormat, bool bUseSRGB = false);
	static bool CombineTextures(TArray<FColor>& OutPixels, const TArray<FGLTFTextureCombineSource>& Sources, const FIntPoint& OutputSize, EPixelFormat OutputPixelFormat);
	static FGLTFPropertyBakeOutput BakeMaterialProperty(const FIntPoint& OutputSize, EMaterialProperty Property, const UMaterialInterface* Material, bool bCopyAlphaFromRedChannel = false);

	static FGLTFJsonTextureIndex AddCombinedTexture(FGLTFConvertBuilder& Builder, const TArray<FGLTFTextureCombineSource>& CombineSources, const FIntPoint& TextureSize, const FString& TextureName, EGLTFJsonTextureFilter MinFilter, EGLTFJsonTextureFilter MagFilter, EGLTFJsonTextureWrap WrapS, EGLTFJsonTextureWrap WrapT);
	static FGLTFJsonTextureIndex AddTexture(FGLTFConvertBuilder& Builder, const TArray<FColor>& Pixels, const FIntPoint& TextureSize, const FString& TextureName, EPixelFormat PixelFormat, EGLTFJsonTextureFilter MinFilter, EGLTFJsonTextureFilter MagFilter, EGLTFJsonTextureWrap WrapS, EGLTFJsonTextureWrap WrapT);

	static FLinearColor GetMask(const FExpressionInput& ExpressionInput);
	static uint32 GetMaskComponentCount(const FExpressionInput& ExpressionInput);
};
